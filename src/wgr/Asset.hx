package wgr;

#if cpp
import cpp.ConstCharStar;
#end

// wgr_asset.h — making a file local before it is loaded

class Asset {
	// The key every callback carries in its `void *`, and the tables it looks up in.
	// Shared, because a ping crosses on both targets now; `pending` and `fetcher`
	// belong to the two calls that are still hxcpp only.
	static var nextId = 1;
	static var pings = new Map<Int, (host:String, ms:Float) -> Void>();

	#if cpp
	static var pending = new Map<Int, {onSuccess:(path:String) -> Void, onFailure:(path:String) -> Void}>();
	static var fetcher:(request:Handle, url:String, destPath:String) -> Void;
	#end

	/**
		Where relative asset paths resolve from. A URL ("https://host/assets") is a
		fetch origin on both platforms: a missing file is downloaded from it and cached,
		on the web in the browser's storage (IndexedDB, checked as `setCacheMode` says)
		and on desktop in the cache directory, by your fetcher. Anything else is a local
		directory. Pass the same logical paths everywhere; only the base differs.
	**/
	public static inline function getHost():String
		return Raw.wgr_asset_get_host();

	static inline function set_host(v:String):String {
		// Through setHost, not straight to Raw: the two are the same operation and a
		// URL arriving by one route and not the other is exactly the kind of gap
		// nobody finds until the assets do not load.
		setHost(v);
		return v;
	}

	/**
		Milliseconds per frame spent finishing loads on the main thread — GPU uploads.
		4 by default. At least one step runs each frame, so one large texture can
		overrun it: a 4096x4096 texture is a single upload of about 45 ms.
	**/
	public static inline function setUploadBudget(value:Float):Void
		Raw.wgr_asset_set_upload_budget(value);

	/** Where relative asset paths resolve from: a directory or a URL base. **/
	public static function setHost(host:String):Void {
		#if (sys && !emscripten)
		needsFetcher(host);
		#end
		Raw.wgr_asset_set_host(host);
	}

	#if (sys && !emscripten)
	static var fetcherInstalled = false;
	static var fetcherWarned = false;

	/**
		An http(s) URL on a native build needs a downloader, because wgrender links none.

		Every way one reaches wgrender passes through here: a URL host, a `fetchUrl`
		handed to `ensureAsync` or `GuestAbi.loadAsset`, and a redirect whose target is
		a URL. Those are the only moments a fetcher can be needed, since wgrender
		consults one only for a URL host or a task with a URL of its own
		(wgr_asset.c). A local host -- a directory or a file: URL -- and a relative
		`fetchUrl` under one are read where they are, and never reach it.

		With `-D WGR_INCLUDE_FETCHER` the binding installs `httpFetcher` the first
		time, unless the program installed its own with `setFetcher` — whichever order
		the two calls come in, the program's stays.

		Without the define, nothing is installed and nothing is linked: the reference
		to `httpFetcher` is inside the `#if`, so `-dce full` leaves mbedtls out
		entirely. Measured: with it always installed, a guest that never downloads
		anything grows 2,807,448 -> 4,588,784 bytes. That is why this is a define and
		not the default.

		The warning is the other half. Hand wgrender a URL without a fetcher and its
		miss path just resolves the task — as failed, or from whatever is local — with
		no complaint about the one thing that was missing. Saying it once costs
		nothing and is the difference between a puzzle and a sentence.
	**/
	/** http or https, the only URLs a native build downloads. **/
	static function isHttp(url:String):Bool {
		final lower = url == null ? "" : url.toLowerCase();
		return StringTools.startsWith(lower, "http://") || StringTools.startsWith(lower, "https://");
	}

	@:allow(wgr.GuestAbi)
	static function needsFetcher(source:String):Void {
		if (fetcherInstalled || !isHttp(source))
			return;
		#if WGR_INCLUDE_FETCHER
		setFetcher(httpFetcher);
		#else
		if (fetcherWarned)
			return;
		fetcherWarned = true;
		Log.warn('"$source" is a URL and this build has no fetcher, so a miss will fail. '
			+ 'Build with -D WGR_INCLUDE_FETCHER, or install one with Asset.setFetcher.');
		#end
	}
	#end

	/**
		Where downloads land on desktop, and where later runs find them — created as
		needed. By default the user's cache directory for this program,
		<cache>/<company>/<app> (`Wgr.setAppCompany`, `Wgr.setAppName`): ~/.cache or
		$XDG_CACHE_HOME on Linux, ~/Library/Caches on macOS, %LOCALAPPDATA%\...\cache
		on Windows. Ignored on the web, which caches in the browser. Set it before the
		first `host` that is a URL.
	**/
	public static inline function setCacheDir(dir:String):Bool
		return Raw.wgr_asset_set_cache_dir(dir);

	/** The directory downloads go in: set, or the default. "" on the web. **/
	public static inline function getCacheDir():String
		return Raw.wgr_asset_get_cache_dir().toString();

	/**
		Forget a cached asset, so the next ensure fetches it again — the cache's copy,
		never a local host's own file, which is only ever read. False when there was no
		such file, or for a path that isn't under the host (as `ensureAsync` reads one).
		A cache can hold a file that is wrong rather than old — a host that compresses
		once served gzip bytes under an asset's name — and since the host says it hasn't
		changed, revalidation keeps it: only something that drops it helps. wgrender
		drops an entry itself when a loader rejects a cached file, so this is for a
		program that knows better: a new version of an asset, or a user asking to free
		the space.
	**/
	public static inline function evict(path:String):Bool
		return Raw.wgr_asset_evict(path);

	/**
		Tell wgrender a fetch finished. Only a fetcher set with `setFetcher` is handed a
		`request` to report on, so this is for hxcpp; finishing on a later tick is
		expected, and nothing blocks meanwhile.
	**/
	public static inline function fetchDone(request:Handle, ok:Bool):Bool
		return Raw.wgr_asset_fetch_done(request, ok);

	public static inline function clearCache():Void
		Raw.wgr_asset_clear_cache();

	/**
		How a cached asset is treated on a later visit: checked with the host unless
		still fresh (`Revalidate`, the default), used as it is (`Trust`), or not kept
		between visits (`Off`). The web only, for now; see `AssetCacheMode`. A mode
		applies to every file checked after it is set. False for a value that isn't
		one of the modes.
	**/
	public static inline function setCacheMode(mode:AssetCacheMode):Bool
		return Raw.wgr_asset_set_cache_mode(mode);

	public static inline function getCacheMode():AssetCacheMode
		return AssetCacheMode.of(Raw.wgr_asset_get_cache_mode());

	/**
		An asset manifest: a hash of each file's contents, so a cached copy whose hash
		still matches is used with no request at all, and one that changed is fetched
		once. `path` is the root manifest's logical path under the host
		("manifest.json"); tools/gen_manifest.py writes the manifests, one per
		directory. The root is asked about once per run; a directory's manifest only
		when a file under it is first ensured, and only if it changed. A listed file is
		hashed before it is kept, and bytes that don't match fail the load. What no
		manifest lists is cached as the cache mode says. Natively it needs a URL host
		and a fetcher; a directory host ignores it.

		`null` or "" for none. False for a path that isn't relative (one starting with
		"/" or holding "://"), or is 512 bytes or longer. Set it after `setHost` and
		before the ensures it should cover.
	**/
	public static inline function setManifest(path:String):Bool
		return Raw.wgr_asset_set_manifest(path);

	/**
		Load files whose path starts with `prefix` from under `target` instead — mods,
		translations, a CDN. A `target` containing "://" is where the file downloads
		from — the browser on the web, your fetcher on desktop — and it is still cached
		and loaded under its own path.

		Path rules stack: every one matching a file is tried, the one added last first,
		then the file's own path, so later rules sit on top. A miss under a path rule
		isn't an error, just the next rule (on the web that costs a request). Download
		rules don't stack — the newest one matching wins. Prefixes are plain text
		matched at the start of the path, not globs. Up to 32 rules; false when full,
		given an empty prefix or target, a prefix over 255 or target over 511
		characters, or a prefix or path target that isn't under the host (as
		`ensureAsync` reads a path; a trailing "/" is kept). Every refusal is logged. Adding the same prefix twice keeps both
		rules rather than replacing the first.
	**/
	public static function addRedirect(prefix:String, target:String):Bool {
		#if (sys && !emscripten)
		needsFetcher(target);
		#end
		return Raw.wgr_asset_add_redirect(prefix, target);
	}

	public static inline function clearRedirects():Void
		Raw.wgr_asset_clear_redirects();

	/**
		Roughly how far a task or group has got, 0 to 1, for a loading screen. A file
		counts a quarter each for being fetched, for its dependencies, for being
		prepared and for being finished, and reads 1 once the task has completed and
		its handle is no longer live.
	**/
	public static inline function getProgress(task:AssetTask):Float
		return Raw.wgr_asset_get_progress(task);

	/**
		One task standing for many files — a level, or a loading screen. It completes
		when all its members have, successfully only if they all did, and then fires its
		own callbacks with an empty path. Members keep their own callbacks if they have
		any. The group holds its members' resources until its callbacks have run, so
		they can be created there.
	**/
	public static inline function createGroup():AssetTask
		return (Raw.wgr_asset_group_create() : Handle);

	/** Add a file task to a group. False for anything else, or a task already in one. **/
	public static inline function groupAdd(group:AssetTask, task:AssetTask):Bool
		return Raw.wgr_asset_group_add(group, task);

	/**
		Make a file local, then report. Returns a task to watch or attach callbacks to,
		or none on failure.

		`path` is the logical key: the cache path on the web, the read path under the
		host on desktop, and where a fetched file lands. It stays under the host: "\\"
		is read as "/", and "." and ".." are resolved; a path that is absolute, names a
		drive, or climbs above the host is refused (no task).

		`fetchUrl` overrides only where the bytes come *from* — a mirror, a signed link,
		a versioned name — and leaving it null means the host plus the path, with
		redirects and per-device variants applied. Null is not the same as "": passing
		one tells wgrender the caller chose this exact file.

		It is read against the host as a browser reads a URL against a directory, on
		every platform: "music/v2/a.mp3" is under the host, "../x" beside it, "/x" at
		its origin's root, and an absolute URL is used as it is. On desktop an absolute
		one has to be http or https, and needs a fetcher (`setFetcher`) but not a URL
		host. Under a local host a relative one is a file under it, read where it is;
		it is held to `path`'s rules, so it can't climb out. A `fetchUrl` that is
		refused — a file: URL, one leaving a local host — means no task.
	**/
	public static function ensureAsync(path:String, ?fetchUrl:String, ?flags:AssetFlag):AssetTask {
		#if (sys && !emscripten)
		needsFetcher(fetchUrl);
		#end
		return (Raw.wgr_asset_ensure_async(path, #if cpp Native.cstr(fetchUrl) #else fetchUrl #end,
			flags == null ? 0 : (flags : Int)) : Handle);
	}

	/**
		Time the round trip to an asset host: `onDone` fires on a later frame with the
		milliseconds, or a negative number when it couldn't be reached inside
		`timeoutMs` (0 or less means 5000). A null `host` pings the current one.

		On the web it is a HEAD request, and any response counts, even a 404. On desktop
		the host is a local directory: 0 if it exists, negative if not — a URL host
		can't be pinged there, since the fetcher hook deals in files rather than round
		trips, so time an `ensureAsync` instead. False when eight pings are already
		waiting.
	**/
	public static function pingHost(?host:String, timeoutMs:Int = 0, onDone:(host:String, ms:Float) -> Void):Bool {
		final id = nextId++;
		pings.set(id, onDone);
		final ok = Raw.wgr_asset_ping_host(#if cpp Native.cstr(host) #else host #end, timeoutMs,
			Trampoline.ping(pingTrampoline), Native.toUser(id));
		if (!ok)
			pings.remove(id);
		return ok;
	}

	// One ping per call, so drop the closure as it fires.
	static function pingTrampoline(host:CStr, milliseconds:F32, user:VoidStar):Void {
		final id = Native.fromUser(user);
		final cb = pings.get(id);
		if (cb == null)
			return;
		pings.remove(id);
		try
			cb(#if cpp host.toString() #else Raw.str(host) #end, milliseconds)
		catch (e:haxe.Exception)
			Wgr.report("an asset ping callback", e);
	}

	/**
		Download a missing asset. wgrender calls `fetch` when a file isn't local yet,
		there is somewhere to download it from, and the platform has no downloader of
		its own — which is every desktop build. Somewhere to download from means a URL
		`host`, or a source this task was handed outright: a `fetchUrl` passed to
		`ensureAsync`, or a redirect target containing "://". Fetch the URL into the
		destination path, then call `fetchDone` with the same request and whether it
		worked; finishing on a later tick is expected, and nothing blocks meanwhile.

		Bytes never cross this boundary: a downloader deals in files, which is what
		curl, WinHTTP and NSURLSession all hand you anyway, and the directories above
		the destination already exist. The destination is where the download is
		written until it is whole, not where the file is read: wgrender moves it into
		place when you report success and deletes it when you don't, so a failed or
		interrupted download never leaves half a file and never costs the copy that
		was there. Success with nothing written is a failure.

		Without one, a miss on desktop fails as it always has.

		Returns false on the web, where there is nothing to install: the browser is the
		downloader, and wgrender fetches a miss itself. It compiles there so that a
		program wanting a fetcher on desktop does not have to put `#if` around the one
		line that says so — the parts that really are native, like reading an
		environment variable or running a program, are Haxe's own business and guard
		themselves.
	**/
	public static function setFetcher(fetch:(request:Handle, url:String, destPath:String) -> Void):Bool {
		#if cpp
		fetcher = fetch;
		#if !emscripten
		fetcherInstalled = fetch != null; // so needsFetcher leaves this one alone
		#end
		return Raw.wgr_asset_set_fetcher(cpp.Callable.fromStaticFunction(fetchTrampoline), Native.nullPtr());
		#else
		return false;
		#end
	}

	#if (sys && !emscripten)
	/**
		A fetcher, ready to install: `Asset.setFetcher(Asset.httpFetcher)`.

		wgrender links no HTTP and no TLS, so it asks the program to download a miss.
		In C that means finding a client; in Haxe the standard library is one, and on
		hxcpp `haxe.Http` does HTTPS through `sys.ssl.Socket` with hxcpp's bundled
		mbedtls. So the hook stays -- it is how wgrender asks -- but nobody has to
		write the answer.

		It costs about a megabyte of mbedtls in the binary, and only if you name it:
		`-dce full` leaves it out of a program that never installs it, which is why it
		can sit here rather than being switched on by default. Certificates are
		verified (`sys.ssl.Socket.DEFAULT_VERIFY_CERT`), from the system store on
		Windows and macOS and from the usual bundle paths elsewhere.

		Redirects are followed as a browser's fetch follows them, so a URL gets the same
		file on desktop as on the web: 301, 302, 303, 307 and 308, up to
		`MAX_REDIRECTS` hops, https to http included. Anything else that isn't a 2xx
		fails the fetch: haxe.Http counts every status under 400 a success, so left to
		itself it would save a redirect's body as the asset.

		Synchronous, so it blocks the frame it runs on -- fine for a handful of small
		files, wrong for a large one. The hook is built for the other way round: start
		a download and call `fetchDone` from a later tick. Wrap this, or write your
		own, when that matters.
	**/
	public static function httpFetcher(request:Handle, url:String, destPath:String):Void {
		// Either way: a false is what makes the asset fail rather than wait for ever.
		fetchDone(request, download(url, destPath));
	}

	/**
		How many redirects `httpFetcher` follows before it gives up: 10, where a browser
		takes 20, as the systems' own HTTP behind wgrender-nim's fetcher (WinHTTP,
		libcurl) stop at 10, and a program should get the same file from either binding.
	**/
	public static inline final MAX_REDIRECTS = 10;

	static function download(url:String, destPath:String):Bool {
		var at = url;
		for (_ in 0...MAX_REDIRECTS + 1) {
			// a browser's fetch fails a redirect anywhere else too; and haxe.Http would
			// read "file" in file:///x as a host name
			if (!isHttp(at))
				return fetchFailed(url, at, 'not an http or https URL');
			var status = 0;
			var body:haxe.io.Bytes = null;
			var failure:String = null;
			final http = new haxe.Http(at);
			http.onStatus = s -> status = s;
			// onBytes, not onData: onData is a String and assets are images and audio.
			http.onBytes = bytes -> body = bytes;
			http.onError = e -> failure = e;
			try
				http.request(false)
			catch (e:haxe.Exception)
				failure = e.message;
			if (failure != null)
				return fetchFailed(url, at, failure);
			if (isRedirect(status)) {
				final location = responseHeader(http, "location");
				if (location == null)
					return fetchFailed(url, at, 'HTTP $status with no Location');
				at = resolveUrl(at, location);
				continue;
			}
			if (status < 200 || status >= 300 || body == null)
				return fetchFailed(url, at, 'HTTP $status');
			try
				sys.io.File.saveBytes(destPath, body)
			catch (e:haxe.Exception)
				return fetchFailed(url, at, 'writing $destPath: ${e.message}');
			return true;
		}
		return fetchFailed(url, at, 'more than $MAX_REDIRECTS redirects');
	}

	/** The statuses a browser's fetch follows; any other 3xx is its own answer. **/
	static inline function isRedirect(status:Int):Bool
		return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;

	static function fetchFailed(url:String, at:String, why:String):Bool {
		Log.error('fetch failed: $url' + (at != url ? ' (at $at)' : '') + ': $why');
		return false;
	}

	/** A response header by name, which HTTP compares without case and haxe.Http with. **/
	static function responseHeader(http:haxe.Http, name:String):Null<String> {
		for (key => value in http.responseHeaders)
			if (key.toLowerCase() == name)
				return value;
		return null;
	}

	/** Where a Location points, read against the URL that sent it (RFC 3986, less dot segments). **/
	static function resolveUrl(base:String, location:String):String {
		if (~/^[A-Za-z][A-Za-z0-9+.-]*:/.match(location))
			return location; // absolute
		final scheme = base.substr(0, base.indexOf(":") + 1);
		if (StringTools.startsWith(location, "//"))
			return scheme + location;
		final authorityEnd = base.indexOf("/", scheme.length + 2);
		final origin = authorityEnd < 0 ? base : base.substr(0, authorityEnd);
		if (StringTools.startsWith(location, "/"))
			return origin + location;
		// relative to the base's directory, without its query or fragment
		var path = authorityEnd < 0 ? "/" : base.substr(authorityEnd);
		path = ~/[?#].*$/.replace(path, "");
		return origin + path.substr(0, path.lastIndexOf("/") + 1) + location;
	}
	#end

	#if cpp
	@:allow(wgr.AssetTask)
	static function addTask(task:Handle, onSuccess:(path:String) -> Void, ?onFailure:(path:String) -> Void):Bool {
		final id = nextId++;
		pending.set(id, {onSuccess: onSuccess, onFailure: onFailure});
		final ok = Raw.wgr_asset_add_task(task, cpp.Callable.fromStaticFunction(successTrampoline),
			cpp.Callable.fromStaticFunction(failureTrampoline), Native.toUser(id)) == 0;
		if (!ok)
			pending.remove(id);
		return ok;
	}

	static function fetchTrampoline(request:WgrHandle, url:ConstCharStar, destPath:ConstCharStar,
			user:VoidStar):Void {
		if (fetcher == null) { // setFetcher(null): fail it, or the task waits for ever
			Raw.wgr_asset_fetch_done(request, false);
			return;
		}
		try
			fetcher((request : Handle), url.toString(), destPath.toString())
		catch (e:haxe.Exception) {
			Wgr.report('the asset fetcher for "${url.toString()}"', e);
			Raw.wgr_asset_fetch_done(request, false);
		}
	}

	// One callback per task, then the task is gone — so drop the closures here.
	static function finish(user:VoidStar, path:ConstCharStar, success:Bool):Void {
		final id = Native.fromUser(user);
		final entry = pending.get(id);
		if (entry == null)
			return;
		pending.remove(id);
		final cb = success ? entry.onSuccess : entry.onFailure;
		if (cb == null)
			return;
		try
			cb(path.toString())
		catch (e:haxe.Exception)
			Wgr.report('an asset callback for "${path.toString()}"', e);
	}

	static function successTrampoline(path:ConstCharStar, user:VoidStar):Void
		finish(user, path, true);

	static function failureTrampoline(path:ConstCharStar, user:VoidStar):Void
		finish(user, path, false);
	#end
}
