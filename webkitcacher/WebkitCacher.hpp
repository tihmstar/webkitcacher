//
//  WebkitCacher.hpp
//  webkitCacher
//
//  Created by tihmstar on 27.12.20.
//

#ifndef WebkitCacher_hpp
#define WebkitCacher_hpp

#include <iostream>
#include <sqlite3.h>
#include <stdint.h>

class WebkitCacher {
    enum ResourceType {
        Master = 1 << 0,
        Manifest = 1 << 1,
        Explicit = 1 << 2,
        Foreign = 1 << 3,
        Fallback = 1 << 4
    };
    std::string _applicationCachePath;
    
    sqlite3 *_db;
    
    void setCacheAllowsAllNetworkRequests0(int cacheID);
    void addOrigin(std::string url);
    int cacheIDForUrl(std::string url);
    
    int resourceIDForUrl(std::string url);
    int getNextFreeCacheResourcesID();
    
    void createCacheResource(int resourceID, std::string resourceURL, std::string mimeType, uint64_t dataSize, int dataresourceID = 0);
    void createCacheResourceData(int resourceID, std::string data);
    void addCachesSize(int chaceID, size_t size);

    void createCacheEntry(int cacheID, ResourceType resourceType, int resourceID);
    
    void addResourceToURL(std::string url, std::string resource, std::string mimeType = "text/html", std::string data = "");
    
    void addDirectoryResourcesRecursive(std::string url, std::string dir);
    
public:
    WebkitCacher(std::string applicationCachePath);
    ~WebkitCacher();
    
    void cacheDirectory(std::string url, std::string dir);
    void addRedirect(std::string url, std::string targetUrl);
};

#endif /* WebkitCacher_hpp */
