//
//  WebkitCacher.cpp
//  webkitCacher
//
//  Created by tihmstar on 27.12.20.
//

#include "WebkitCacher.hpp"
#include <libgeneral/macros.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define RESOURCE_CACHEFILE "cache.cache"

#define sql_exec(sql) \
    { \
        char *errmsg = NULL; \
        cleanup([&]{ \
            safeFreeCustom(errmsg, sqlite3_free); \
        }); \
        retassure(!(sqlite_err = sqlite3_exec(_db, sql, NULL, NULL, &errmsg)), "sql_exec failed on '%s' with error %d (%s)",sql,sqlite_err,errmsg); \
    }

#pragma mark static helpers

static std::string hostForUrl(std::string url){
    std::string host = url;
    ssize_t protocolDelimiter = 0;
    ssize_t pathDelimiter = 0;

    if ((protocolDelimiter = host.find("//")) != std::string::npos) {
        host = host.substr(protocolDelimiter+2);
    }

    if ((pathDelimiter = host.find("/")) != std::string::npos) {
        host = host.substr(0,pathDelimiter);
    }
    return host;
}

static std::string originForUrl(std::string url){
    std::string origin = url;
    ssize_t protocolDelimiter = 0;
    ssize_t pathDelimiter = 0;

    if ((protocolDelimiter = origin.find("://")) != std::string::npos) {
        origin = origin.substr(0,protocolDelimiter) + "_" + origin.substr(protocolDelimiter+3);
    }

    if ((pathDelimiter = origin.find("/")) != std::string::npos) {
        origin = origin.substr(0,pathDelimiter);
    }
    
    origin += "_0";
    
    return origin;
}


int webkitHashString(std::string s){
    unsigned int hash = 0x9E3779B9U;
    bool remainder = s.size() & 1;

    if (s.size()) {
        for (int i=0; i<s.size()-1; i+=2) {
            hash += ((unsigned char)s.at(i));
            hash = (hash << 16) ^ ((((unsigned char)s.at(i+1)) << 11) ^ hash);
            hash += hash >> 11;
        }
        
        if (remainder){
            hash += ((unsigned char)s.back());
            hash ^= hash << 11;
            hash += hash >> 17;
        }
    }
    
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 2;
    hash += hash >> 15;
    hash ^= hash << 10;
    
    hash &= (1U << (sizeof(hash) * 8 - 8)) - 1;

    if (!hash)
        hash = 0x80000000 >> 8;
    return hash;
}

#pragma mark WebkitCacher

WebkitCacher::WebkitCacher(std::string applicationCachePath)
: _applicationCachePath(applicationCachePath),
    _db(NULL)
{
    int sqlite_err = 0;

    //open or create DB
    assure(!(sqlite_err = sqlite3_open(_applicationCachePath.c_str(), &_db)));

    //create database structure
    
    //create tables
    sql_exec("CREATE TABLE IF NOT EXISTS CacheAllowsAllNetworkRequests (wildcard INTEGER NOT NULL ON CONFLICT FAIL, cache INTEGER NOT NULL ON CONFLICT FAIL)");
    sql_exec("CREATE TABLE IF NOT EXISTS CacheEntries (cache INTEGER NOT NULL ON CONFLICT FAIL, type INTEGER, resource INTEGER NOT NULL)");
    sql_exec("CREATE TABLE IF NOT EXISTS CacheGroups (id INTEGER PRIMARY KEY AUTOINCREMENT, manifestHostHash INTEGER NOT NULL ON CONFLICT FAIL, manifestURL TEXT UNIQUE ON CONFLICT FAIL, newestCache INTEGER, origin TEXT)");
    sql_exec("CREATE TABLE IF NOT EXISTS CacheResourceData (id INTEGER PRIMARY KEY AUTOINCREMENT, data BLOB, path TEXT)");
    sql_exec("CREATE TABLE IF NOT EXISTS CacheResources (id INTEGER PRIMARY KEY AUTOINCREMENT, url TEXT NOT NULL ON CONFLICT FAIL, statusCode INTEGER NOT NULL, responseURL TEXT NOT NULL, mimeType TEXT, textEncodingName TEXT, headers TEXT, data INTEGER NOT NULL ON CONFLICT FAIL)");
    sql_exec("CREATE TABLE IF NOT EXISTS CacheWhitelistURLs (url TEXT NOT NULL ON CONFLICT FAIL, cache INTEGER NOT NULL ON CONFLICT FAIL)");
    sql_exec("CREATE TABLE IF NOT EXISTS Caches (id INTEGER PRIMARY KEY AUTOINCREMENT, cacheGroup INTEGER, size INTEGER)");
    sql_exec("CREATE TABLE IF NOT EXISTS DeletedCacheResources (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT)");
    sql_exec("CREATE TABLE IF NOT EXISTS FallbackURLs (namespace TEXT NOT NULL ON CONFLICT FAIL, fallbackURL TEXT NOT NULL ON CONFLICT FAIL, cache INTEGER NOT NULL ON CONFLICT FAIL)");
    sql_exec("CREATE TABLE IF NOT EXISTS Origins (origin TEXT UNIQUE ON CONFLICT IGNORE, quota INTEGER NOT NULL ON CONFLICT FAIL)");

    //create triggers
    sql_exec("CREATE TRIGGER IF NOT EXISTS CacheDeleted AFTER DELETE ON Caches FOR EACH ROW BEGIN DELETE FROM CacheEntries WHERE cache = OLD.id; DELETE FROM CacheWhitelistURLs WHERE cache = OLD.id; DELETE FROM CacheAllowsAllNetworkRequests WHERE cache = OLD.id; DELETE FROM FallbackURLs WHERE cache = OLD.id; END");
    sql_exec("CREATE TRIGGER IF NOT EXISTS CacheEntryDeleted AFTER DELETE ON CacheEntries FOR EACH ROW BEGIN DELETE FROM CacheResources WHERE id = OLD.resource; END");
    sql_exec("CREATE TRIGGER IF NOT EXISTS CacheResourceDataDeleted AFTER DELETE ON CacheResourceData FOR EACH ROW WHEN OLD.path NOT NULL BEGIN INSERT INTO DeletedCacheResources (path) values (OLD.path); END");
    sql_exec("CREATE TRIGGER IF NOT EXISTS CacheResourceDeleted AFTER DELETE ON CacheResources FOR EACH ROW BEGIN DELETE FROM CacheResourceData WHERE id = OLD.data; END");
}

WebkitCacher::~WebkitCacher(){
    safeFreeCustom(_db, sqlite3_close);
}

#pragma mark private

void WebkitCacher::setCacheAllowsAllNetworkRequests0(int cacheID){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    int sqlite_err = 0;
    int wildcard = -1; //needs to be non 0 so we can detect 0 case from SQL query later
    
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,"SELECT * FROM CacheAllowsAllNetworkRequests WHERE cache = ?;",-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, cacheID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    if (sqlite3_step(stmt) == SQLITE_ROW){
        wildcard = sqlite3_column_int(stmt, 0);
    }
    safeFreeCustom(stmt, sqlite3_finalize);
    
    if (wildcard) { // make sure wildcard entry 0 exists
        retassure(!(sqlite_err = sqlite3_prepare_v2(_db,"INSERT OR REPLACE INTO CacheAllowsAllNetworkRequests(wildcard, cache)"
                                                        "VALUES(0, ?);",-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
        retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, cacheID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
        retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
        safeFreeCustom(stmt, sqlite3_finalize);
    }
}

void WebkitCacher::addOrigin(std::string url){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    int sqlite_err = 0;
    std::string origin;
    
    origin = originForUrl(url);

    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "INSERT INTO Origins (origin, quota) "
                                                "SELECT ?,0 "
                                                "WHERE NOT EXISTS (SELECT * FROM 'Origins' WHERE origin = ?);"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 1, origin.c_str(),(int)origin.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 2, origin.c_str(),(int)origin.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);

    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "UPDATE Origins "
                                                "SET origin = ?, quota = 0 "
                                                "WHERE origin = ?;"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 1, origin.c_str(),(int)origin.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 2, origin.c_str(),(int)origin.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);
}


int WebkitCacher::cacheIDForUrl(std::string url){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    std::string host;
    unsigned int manifestHostHash = 0;
    int sqlite_err = 0;
    int latestId = 0;
    
    if (url.back() != '/') url += '/';

    host = hostForUrl(url);
    manifestHostHash = webkitHashString(host);
    
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,"SELECT * FROM 'CacheGroups' WHERE manifestHostHash = ?;",-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, manifestHostHash)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    if (sqlite3_step(stmt) == SQLITE_ROW){
        return sqlite3_column_int(stmt, 0);
    }
    safeFreeCustom(stmt, sqlite3_finalize);
    
    //When we got here, that means this host doesn't exist yet
    
    //get latest ID
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,"SELECT * FROM 'CacheGroups' ORDER BY id DESC LIMIT 0, 1;",-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    if (sqlite3_step(stmt) == SQLITE_ROW){
        latestId = sqlite3_column_int(stmt, 0);
    }
    safeFreeCustom(stmt, sqlite3_finalize);

    //now latestId points to the highest used ID
    latestId++; //increment to use next free ID
    std::string origin = originForUrl(url);
    
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,"INSERT INTO CacheGroups(id, manifestHostHash, manifestURL, newestCache, origin)"
                                                    "VALUES(?,?,?,?,?);",-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, latestId)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 2, manifestHostHash)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    {
        std::string arg = url+RESOURCE_CACHEFILE;
        retassure(!(sqlite_err = sqlite3_bind_text(stmt, 3, arg.c_str(),(int)arg.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    }
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 4, latestId)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 5, origin.c_str(),(int)origin.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);

    return latestId;
}

int WebkitCacher::resourceIDForUrl(std::string url){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    int sqlite_err = 0;
    int resourceID = 0; //real resourceIDs can't be zero
        
    //check if resource is already there
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,"SELECT * FROM 'CacheResources' WHERE url = ?;",-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 1, url.c_str(),(int)url.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(sqlite3_step(stmt) == SQLITE_ROW, "No resourceID found for URL '%s'",url.c_str());
    resourceID = sqlite3_column_int(stmt, 0);
    safeFreeCustom(stmt, sqlite3_finalize);
    return resourceID;
}

int WebkitCacher::getNextFreeCacheResourcesID(){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    int sqlite_err = 0;
    int resourceID = 0; //real resourceIDs can't be zero

    //get highest used ID and increment to get next free resourceID
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,"SELECT * FROM 'CacheResources' ORDER BY id DESC LIMIT 0, 1;",-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    if (sqlite3_step(stmt) == SQLITE_ROW){
        resourceID = sqlite3_column_int(stmt, 0);
    }
    safeFreeCustom(stmt, sqlite3_finalize);
    resourceID++; //now points to free usable resourceID
    return resourceID;;
}


void WebkitCacher::createCacheResource(int resourceID, std::string resourceURL, std::string mimeType, uint64_t dataSize, int dataresourceID){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    int sqlite_err = 0;
    
    if (!dataresourceID) dataresourceID = resourceID;
    
    std::string headers;
//    headers += "Last-Modified:Fri, 25 Dec 2020 10:51:00 GMT";
//    headers += "Date:Sat, 26 Dec 2020 14:46:24 GMT";
    headers += "Accept-Ranges:bytes\n";
    headers += "Content-Type:"+mimeType+"\n";
    headers += "Content-Length:"+std::to_string(dataSize)+"\n";
    
    //make sure at least a dummy entry exists
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "INSERT INTO CacheResources (id, url, statusCode, responseURL, mimeType, textEncodingName, headers, data) "
                                                "SELECT ?,\"\",200,\"\",\"\",\"\",\"\",? "
                                                "WHERE NOT EXISTS (SELECT * FROM 'CacheResources' WHERE id = ?);"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 2, dataresourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 3, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);
    
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "UPDATE CacheResources "
                                                "SET url = ?, statusCode = 200, responseURL = ?, mimeType = ?, headers = ?, data = ? "
                                                "WHERE id = ?;"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 1, resourceURL.c_str(),(int)resourceURL.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 2, resourceURL.c_str(),(int)resourceURL.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 3, mimeType.c_str(),(int)mimeType.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_text(stmt, 4, headers.c_str(),(int)headers.size(),SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 5, dataresourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 6, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);
}

void WebkitCacher::createCacheResourceData(int resourceID, std::string data){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    int sqlite_err = 0;
    
    //make sure at least a dummy entry exists
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "INSERT INTO CacheResourceData (id, data, path) "
                                                "SELECT ?,\"\",NULL "
                                                "WHERE NOT EXISTS (SELECT * FROM 'CacheResourceData' WHERE id = ?);"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 2, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);

    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "UPDATE CacheResourceData "
                                                "SET data = ? "
                                                "WHERE id = ?;"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_blob(stmt, 1, data.data(), (int)data.size(), SQLITE_TRANSIENT)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 2, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);
}

void WebkitCacher::addCachesSize(int chaceID, size_t size){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    int sqlite_err = 0;
    size_t currentCacheSize = 0;
    
    //make sure at least a dummy entry exists
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "INSERT INTO Caches (id, cacheGroup, size) "
                                                "SELECT ?,?,0 "
                                                "WHERE NOT EXISTS (SELECT * FROM 'Caches' WHERE cacheGroup = ?);"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, chaceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 2, chaceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 3, chaceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);
    
    //get current size
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,"SELECT * FROM 'Caches' WHERE cacheGroup = ?;",-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, chaceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(sqlite3_step(stmt) == SQLITE_ROW, "Caches entry does not exist, but should have been created");
    currentCacheSize = sqlite3_column_int(stmt, 2);
    safeFreeCustom(stmt, sqlite3_finalize);

    currentCacheSize += size;
    
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "UPDATE Caches "
                                                "SET size = ? "
                                                "WHERE id = ?;"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, (int)currentCacheSize)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 2, chaceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);
}


void WebkitCacher::createCacheEntry(int cacheID, ResourceType resourceType, int resourceID){
    sqlite3_stmt *stmt = NULL;
    cleanup([&]{
        safeFreeCustom(stmt, sqlite3_finalize);
    });
    int sqlite_err = 0;
    
    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "INSERT INTO CacheEntries (cache, type, resource) "
                                                "SELECT ?,?,? "
                                                "WHERE NOT EXISTS (SELECT * FROM 'CacheEntries' WHERE resource = ?);"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, cacheID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 2, resourceType)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 3, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 4, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);

    retassure(!(sqlite_err = sqlite3_prepare_v2(_db,
                                                "UPDATE CacheEntries "
                                                "SET cache = ?, type = ?, resource = ? "
                                                "WHERE resource = ?;"
                                                ,-1,&stmt,NULL)), "Failed to prepare SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 1, cacheID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 2, resourceType)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 3, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure(!(sqlite_err = sqlite3_bind_int(stmt, 4, resourceID)),"Failed to bind arg to prepared SQL statement with error=%d",sqlite_err);
    retassure((sqlite_err = sqlite3_step(stmt)) == SQLITE_DONE, "Failed to execute satement");
    safeFreeCustom(stmt, sqlite3_finalize);
}


void WebkitCacher::addResourceToURL(std::string url, std::string resource, std::string mimeType, std::string data){
    int resourceID = 0; //real resourceIDs can't be zero
    int cacheID = 0;
    ResourceType resourceType = ResourceType::Master;
    
    if (url.back() != '/') url += '/';
    if (resource.front() == '/') resource = resource.substr(1);
    url += resource;
    
    if (resource == RESOURCE_CACHEFILE) {
        resourceType = ResourceType::Manifest;
    }
    
    try {
        resourceID = resourceIDForUrl(url);
    } catch (...) {
        resourceID = getNextFreeCacheResourcesID();
    }
    
    cacheID = cacheIDForUrl(url);
    createCacheEntry(cacheID, resourceType, resourceID);

    createCacheResource(resourceID, url, mimeType, data.size());
    createCacheResourceData(resourceID, data);
    addCachesSize(cacheID, data.size());
}

void WebkitCacher::addDirectoryResourcesRecursive(std::string url, std::string dir){
    DIR *d = NULL;
    cleanup([&]{
        safeFreeCustom(d, closedir);
    });
    struct dirent *dfile = NULL;
    
    if (url.back() != '/') url += '/';
    if (dir.back() != '/') dir += '/';
    
    retassure(d = opendir(dir.c_str()), "Failed to open dir with err=%d (%s)",errno,strerror(errno));
    
    while ((dfile = readdir(d))) { //remove all subdirs and files
        if (strcmp(dfile->d_name, ".") == 0 || strcmp(dfile->d_name, "..") == 0) {
            continue;
        }
        std::string filepath = dir + dfile->d_name;
        
        if (dfile->d_type == DT_DIR) {
            addDirectoryResourcesRecursive(url + dfile->d_name, filepath);
        }else{
            int fd = -1;
            cleanup([&]{
                if (fd > 0) {
                    close(fd);fd=-1;
                }
            });
            struct stat st = {};
            std::string filedata;
            
            retassure((fd = open(filepath.c_str(), O_RDONLY)) > 0, "Failed to open file '%s'",filepath.c_str());
            retassure(!fstat(fd, &st), "Failed to stat file '%s'",filepath.c_str());
            
            filedata.resize(st.st_size);
            retassure(read(fd, (void*)filedata.data(), filedata.size()) == filedata.size(), "failed to read file");
            addResourceToURL(url, dfile->d_name, "text/html", filedata);
        }
    }
}


#pragma mark public

void WebkitCacher::cacheDirectory(std::string url, std::string dir){
    int cacheID = 0;

    if (url.back() != '/') url += '/';
    if (dir.back() != '/') dir += '/';

    cacheID = cacheIDForUrl(url);
    setCacheAllowsAllNetworkRequests0(cacheID);
    addOrigin(url);

    addResourceToURL(url, RESOURCE_CACHEFILE, "application/octet-stream", "CACHE MANIFEST\n# v2.5.5 Self-Host\n");
    
    addDirectoryResourcesRecursive(url, dir);
}

void WebkitCacher::addRedirect(std::string url, std::string targetUrl){
    int cacheID = 0;
    int srcResourceID = 0;
    int dstResourceID = 0;
    
    cacheID = cacheIDForUrl(url);
    setCacheAllowsAllNetworkRequests0(cacheID);
    addOrigin(url);

    dstResourceID = resourceIDForUrl(targetUrl);
    srcResourceID = getNextFreeCacheResourcesID();
    
    std::string redirect = "you should not see this";
    createCacheEntry(cacheID, ResourceType::Master, srcResourceID);
    createCacheResource(srcResourceID, url, "text/html", 0, dstResourceID);
}
