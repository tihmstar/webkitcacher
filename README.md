

**Usage:**
```
webkitcacher version: 0.1-fe654ab7e3231107b7c5e0dabeefbf038fca29c3
Usage: webkitcacher [OPTIONS] <ApplicationCache.db file>
Creates webkit ApplicationCache.db from directory

  -h, --help				prints usage information
  -d, --dir <directory>			directory to cache
  -u, --url <url>			URL where cached content will be accessible
  -r, --redirect <srcurl=dsturl>	adds a redirect from srcurl to cached dsturl
```

**Example:**

```
tMBP:kk tihmstar$ ls
webdir
tMBP:kk tihmstar$ ls  webdir/
702.js			favicon.ico		jb			linux702.js		netcat702-mira.html	spoofer.html
Cache.manifest		ftp.html		jb.html			mira			netcat702.html		spoofer.js
README.md		ftp.js			jb702			mira.html		oldjb			todex.bin
common			hen702.html		linux.html		mira702			ps4-hen-vtx-702.js	todex.html
fakeusb.html		index.html		linux.js		mira702.html		ps4-hen-vtx.html	todex.js
fakeusb.js		index702.html		linux702.html		netcat.html		ps4-hen-vtx.js		webkit-7.02
tMBP:kk tihmstar$ webkitcacher -d webdir/ -u http://cache -r http://cache=http://cache/index702.html -r http://cache/=http://cache/index702.html
webkitcacher version: 0.1-fe654ab7e3231107b7c5e0dabeefbf038fca29c3
Caching directoy 'webdir/' to URL 'http://cache'
Redirecting 'http://cache' to 'http://cache/index702.html'
Redirecting 'http://cache/' to 'http://cache/index702.html'
done!
tMBP:kk tihmstar$ ls
ApplicationCache.db	webdir
tMBP:kk tihmstar$
```
