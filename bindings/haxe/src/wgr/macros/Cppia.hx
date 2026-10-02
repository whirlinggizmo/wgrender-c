package wgr.macros;

#if macro
import haxe.macro.Compiler;
import haxe.macro.Context;
import haxe.macro.Expr;
import haxe.macro.Type;

using Lambda;
using haxe.macro.Tools;
#end

/**
	What a cppia module needs of the binding, for `tools/check_binding.py`'s check-cppia.

	Hot reload ([hotreload-hx](https://github.com/whirlinggizmo/hotreload-hx)) runs an
	application's reloaded code as a cppia module, and cppia can't run an extern call
	or `__cpp__`: the module calls the binding's compiled copies in the executable
	instead. So a wrapper has to have one — `inline` (which hxcpp compiles a copy of)
	and not `extern inline` (which it doesn't; Haxe makes every `overload` that, so an
	overload forwards to an `inline` function that makes the C call) — and a function
	with a C type in its signature has to be in a private class, since `-D scriptable`
	gives every static of a public class a wrapper cppia calls it through, with
	`Dynamic` arguments, and that wrapper doesn't compile for a C pointer, struct, enum
	or function pointer. README, "Calling it from cppia".

	Two init macros, for the module build:

	- `callHost()`: the binding's `inline` functions are called, not inlined into the
	  module (what hotreload-hx does for every library). A function with a C type in
	  its signature is made `extern inline` instead, since cppia could never call it.
	- `reachAll()`: defines a class with one function per public static function of
	  the binding, each calling it once, so that loading the module links every one
	  of them against the executable. A wrapper without a compiled copy fails there,
	  as `Unknown static call`, and one that inlines C fails to compile, as
	  `Unsupported operation in cppia :CppCode`.
**/
class Cppia {
	#if macro
	public static function callHost() {
		Compiler.addGlobalMetadata("wgr", "@:build(wgr.macros.Cppia.build())", true, true, false);
	}

	public static function build():Null<Array<Field>> {
		var ref = Context.getLocalClass();
		if (ref == null)
			return null;
		var cls = ref.get();
		var fields = Context.getBuildFields();
		var changed = false;
		for (field in fields) {
			if (!field.access.contains(AInline) || field.access.contains(AExtern) || field.access.contains(AMacro))
				continue;
			var f = switch (field.kind) {
				case FFun(f): f;
				default: continue;
			}
			if (f.expr == null)
				continue;
			if (hasNativeType(f, field.pos)) {
				field.access.push(AExtern);
				changed = true;
				continue;
			}
			// what has to stay inline: an abstract's constructor (an `inline var` may be
			// made with it) and a function that assigns the abstract's `this`
			if (cls.kind.match(KAbstractImpl(_)) && (field.name == "new" || assignsThis(f.expr)))
				continue;
			field.access.remove(AInline);
			changed = true;
		}
		return changed ? fields : null;
	}

	public static function reachAll() {
		var done = false;
		Context.onAfterTyping(types -> {
			if (done)
				return;
			done = true;
			var fields:Array<Field> = [];
			for (t in types)
				switch (t) {
					case TClassDecl(_.get() => c) if (c.pack[0] == "wgr" && !c.isExtern && !c.isInterface):
						// an abstract's functions are statics of its implementation class, which is
						// private (so hxcpp makes no wrappers for it: cppia reaches them by reflection)
						var owner = switch (c.kind) {
							case KAbstractImpl(_.get() => a) if (!a.isPrivate): a.pack.concat([a.name]);
							case KAbstractImpl(_): continue;
							default: if (c.isPrivate) continue else c.pack.concat([c.name]);
						}
						for (f in c.statics.get()) {
							if (!f.isPublic || f.params.length > 0 || f.name.charAt(0) == "_")
								continue;
							if (f.meta.has(":impl") || f.meta.has(":to") || f.meta.has(":from") || f.meta.has(":op"))
								continue;
							switch (f.kind) {
								case FMethod(MethMacro) | FVar(_, _): continue;
								default:
							}
							for (v in [f].concat(f.overloads.get()))
								switch (Context.follow(v.type)) {
									// (an abstract's instance method is an impl-class static whose first parameter is `this`)
									case TFun(args, ret) if ((args.length == 0 || args[0].name != "this") && !args.exists(a -> isNative(a.t)) && !isNative(ret)):
										var callee = {expr: EConst(CIdent(owner[0])), pos: f.pos};
										for (part in owner.slice(1).concat([f.name]))
											callee = {expr: EField(callee, part), pos: f.pos};
										var call = {
											expr: ECall(callee, args.map(a -> {
												var ct = a.t.toComplexType();
												macro(cast null : $ct);
											})),
											pos: f.pos
										};
										fields.push({
											name: 'reach${fields.length}',
											pos: f.pos,
											access: [AStatic],
											kind: FFun({args: [], ret: null, expr: macro $call})
										});
									default:
								}
						}
					default:
				}
			Context.defineType({
				pack: [],
				name: "WgrCppiaReach",
				pos: Context.currentPos(),
				meta: [{name: ":keep", pos: Context.currentPos()}],
				kind: TDClass(),
				fields: fields
			});
			Sys.println('check-cppia: reaching ${fields.length} functions');
		});
	}

	/** whether a function's parameters or result have a C type, as far as they're written out **/
	static function hasNativeType(f:Function, pos:Position):Bool {
		var types = f.args.map(a -> a.type);
		types.push(f.ret);
		for (t in types) {
			if (t == null)
				continue;
			var type = try Context.resolveType(t, pos) catch (_) null;
			if (type != null && isNative(type))
				return true;
		}
		return false;
	}

	/** a C pointer, struct, enum or function pointer: what can't be `Dynamic` **/
	static function isNative(t:Type):Bool {
		var followed = Context.follow(t);
		return switch (followed) {
			case TInst(_.get() => c, _): c.isExtern && (c.meta.has(":unreflective") || c.meta.has(":structAccess"));
			case TAbstract(_.get() => a, _):
				if (a.meta.has(":callable"))
					true;
				else if (a.meta.has(":coreType"))
					false;
				else
					switch (Context.followWithAbstracts(followed, true)) {
						case TAbstract(_.get() => b, _) if (b.name == a.name && b.pack.join(".") == a.pack.join(".")): false;
						case underlying: isNative(underlying);
					}
			default: false;
		}
	}

	static function assignsThis(e:Expr):Bool {
		var found = false;
		function walk(e:Expr) {
			if (found || e == null)
				return;
			switch (e.expr) {
				case EBinop(OpAssign | OpAssignOp(_), {expr: EConst(CIdent("this"))}, _):
					found = true;
				case EUnop(OpIncrement | OpDecrement, _, {expr: EConst(CIdent("this"))}):
					found = true;
				default:
					haxe.macro.ExprTools.iter(e, walk);
			}
		}
		walk(e);
		return found;
	}
	#end
}
