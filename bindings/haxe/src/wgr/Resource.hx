package wgr;

// wgr_resource.h — what every resource has in common

/**
	Resources load on create: `new Texture(path)` comes back at once, `Pending`, and is
	`Ready` or `Failed` in a later frame, at the start of it. Objects take a resource in
	any status and do the right thing until it's `Ready`, so a program can create
	everything at once and never wait; the status is for what it wants to show.

	Each resource type has this as a method too (`texture.getStatus()`), through
	`@:using`: the same call, not another one.
**/
class Resource {
	/** `Pending`, `Ready` or `Failed` for a resource of any kind; `None` for anything else. **/
	public static inline function getStatus(resource:Handle):ResourceStatus
		return ResourceStatus.of(Raw.wgr_resource_get_status(resource));
}
