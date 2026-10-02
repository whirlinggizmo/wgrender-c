package wgr;

// wgr_resource.h — what every resource has in common

/**
	Resources load on create: `new Texture(path)` comes back at once, `Pending`, and is
	`Ready` or `Failed` in a later frame, at the start of it. Objects take a resource in
	any status and do the right thing until it's `Ready`, so a program can create
	everything at once and never wait; the status is for what it wants to show.

	Each resource type has these as methods too (`texture.getStatus()`,
	`texture.release()`), through `@:using`: the same calls, not other ones.
**/
class Resource {
	/** `Pending`, `Ready` or `Failed` for a resource of any kind; `None` for anything else. **/
	public static inline function getStatus(resource:Handle):ResourceStatus
		return ResourceStatus.of(Raw.wgr_resource_get_status(resource));

	/**
		The file it was read from, as a path under the asset root: for `name.ktx` the
		variant this GPU got (or the PNG), for a redirected path where the redirect
		found it. `""` until it's `Ready`, for one made from numbers, and for anything
		that isn't a resource.
	**/
	public static inline function getPath(resource:Handle):String
		return Raw.wgr_resource_get_path(resource);

	/**
		Drop this handle's reference. A resource is freed when its last reference goes,
		not when you call this; one still loading then stops loading. Objects hold their
		own references, so handing a resource to one and releasing it right away is the
		normal pattern. A built-in (the default texture, the placeholder) is never freed.
		False for a handle that isn't a resource: none, an object, or one already freed.
	**/
	public static inline function release(resource:Handle):Bool
		return Raw.wgr_resource_release(resource);
}
