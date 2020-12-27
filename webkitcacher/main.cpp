//
//  main.cpp
//  webkitCacher
//
//  Created by tihmstar on 27.12.20.
//

#include <stdio.h>
#include "WebkitCacher.hpp"
#include <libgeneral/macros.h>
#include <getopt.h>
#include <vector>

static struct option longopts[] = {
    { "help",           no_argument,        NULL, 'h' },
    { "dir",            required_argument,  NULL, 'd' },
    { "url",            required_argument,  NULL, 'u' },
    { "redirect",       required_argument,  NULL, 'r' },
    { NULL, 0, NULL, 0 }
};


void cmd_help(){
    printf("Usage: webkitcacher [OPTIONS] <ApplicationCache.db file>\n");
    printf("Creates webkit ApplicationCache.db from directory\n\n");
    printf("  -h, --help\t\t\t\tprints usage information\n");
    printf("  -d, --dir <directory>\t\t\tdirectory to cache\n");
    printf("  -u, --url <url>\t\t\tURL where cached content will be accessible\n");
    printf("  -r, --redirect <srcurl=dsturl>\tadds a redirect from srcurl to cached dsturl\n");
}

int main_r(int argc, const char * argv[]) {
    printf("%s\n",VERSION_STRING);
    
    const char *directory = NULL;
    const char *url = NULL;
    const char *lastArg = "ApplicationCache.db";

    int optindex = 0;
    int opt = 0;
    
    std::vector<std::pair<std::string,std::string>> redirects;
 
    
    while ((opt = getopt_long(argc, (char* const *)argv, "hd:u:r:", longopts, &optindex)) > 0) {
        switch (opt) {
            case 'h':
                cmd_help();
                return 0;
            case 'd':
                directory = optarg;
                break;
            case 'u':
                url = optarg;
                break;
            case 'r':
                {
                    std::string cmd = optarg;
                    ssize_t columnpos = cmd.find("=");
                    retassure(columnpos != std::string::npos, "redirect commdn '%s' has invalid format",optarg);
                    redirects.push_back({cmd.substr(0,columnpos),cmd.substr(columnpos+1)});
                }
                break;

            default:
                cmd_help();
                return -1;
        }
    }
    
    if (argc-optind == 1) {
        argc -= optind;
        argv += optind;
        lastArg = argv[0];
    }
    
    if (!url && !directory && !redirects.size()) {
        cmd_help();
        return 0;
    }

    WebkitCacher wk(lastArg);

    if (url && directory) {
        printf("Caching directoy '%s' to URL '%s'\n",directory,url);
        wk.cacheDirectory(url, directory);
    }
    
    for (auto r : redirects) {
        printf("Redirecting '%s' to '%s'\n",r.first.c_str(),r.second.c_str());
        wk.addRedirect(r.first, r.second);
    }
    printf("done!\n");
    return 0;
}

int main(int argc, const char * argv[]) {
#ifdef DEBUG
    return main_r(argc, argv);
#else
    try {
        return main_r(argc, argv);
    } catch (tihmstar::exception &e) {
        printf("%s: failed with exception:\n",PACKAGE_NAME);
        e.dump();
        return e.code();
    }
#endif
}
