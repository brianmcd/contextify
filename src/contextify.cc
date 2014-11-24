#include "node.h"
#include "node_version.h"
#include "nan.h"
#include <string>
using namespace v8;
using namespace node;

// For some reason this needs to be out of the object or node won't load the
// library.
static Persistent<FunctionTemplate> dataWrapperTmpl;
static Persistent<Function>         dataWrapperCtor;

class ContextifyContext : public ObjectWrap {
public:
    Persistent<Context> context;
    Persistent<Object>  sandbox;
    Persistent<Object>  proxyGlobal;

    static Persistent<FunctionTemplate> jsTmpl;

    void* alive;

    ContextifyContext(Local<Object> sbox) {
        NanScope();
        this->alive = this;
        NanAssignPersistent(sandbox, sbox);
    }

    ~ContextifyContext() {
        this->alive = NULL;
        NanDisposePersistent(context);
        NanDisposePersistent(proxyGlobal);
        NanDisposePersistent(sandbox);

        // Provide a GC hint that the context has gone away. Without this call it
        // does not seem that the collector will touch the context until under extreme
        // stress.
        v8::V8::ContextDisposedNotification();
    }

    // We override ObjectWrap::Wrap so that we can create our context after
    // we have a reference to our "host" JavaScript object.  If we try to use
    // handle_ in the ContextifyContext constructor, it will be empty since it's
    // set in ObjectWrap::Wrap.
    inline void Wrap(Handle<Object> handle) {
        ObjectWrap::Wrap(handle);
        Local<Context> lcontext = createV8Context();
        NanAssignPersistent(context, lcontext);
        NanAssignPersistent(proxyGlobal, lcontext->Global());
    }

    // This is an object that just keeps an internal pointer to this
    // ContextifyContext.  It's passed to the NamedPropertyHandler.  If we
    // pass the main JavaScript context object we're embedded in, then the
    // NamedPropertyHandler will store a reference to it forever and keep it
    // from getting gc'd.
    Local<Object> createDataWrapper () {
        NanEscapableScope();
        Local<Object> wrapper = NanNew(dataWrapperCtor)->NewInstance();
        NanSetInternalFieldPointer(wrapper, 0, this);
        return NanEscapeScope(wrapper);
    }

    inline Local<Context> createV8Context() {
        Local<FunctionTemplate> ftmpl = NanNew<FunctionTemplate>();
        ftmpl->SetHiddenPrototype(true);
        ftmpl->SetClassName(NanNew(sandbox)->GetConstructorName());
        Local<ObjectTemplate> otmpl = ftmpl->InstanceTemplate();
        otmpl->SetNamedPropertyHandler(GlobalPropertyGetter,
                                       GlobalPropertySetter,
                                       GlobalPropertyQuery,
                                       GlobalPropertyDeleter,
                                       GlobalPropertyEnumerator,
                                       createDataWrapper());
        otmpl->SetAccessCheckCallbacks(GlobalPropertyNamedAccessCheck,
                                       GlobalPropertyIndexedAccessCheck);
        return NanNewContextHandle(NULL, otmpl);
    }

    static void Init(Handle<Object> target) {
        NanScope();
        Local<FunctionTemplate> tmpl = NanNew<FunctionTemplate>();
        tmpl->InstanceTemplate()->SetInternalFieldCount(1);
        NanAssignPersistent(dataWrapperTmpl, tmpl);
        NanAssignPersistent(dataWrapperCtor, tmpl->GetFunction());

        Local<FunctionTemplate> ljsTmpl = NanNew<FunctionTemplate>(New);
        ljsTmpl->InstanceTemplate()->SetInternalFieldCount(1);
        ljsTmpl->SetClassName(NanNew("ContextifyContext"));
        NODE_SET_PROTOTYPE_METHOD(ljsTmpl, "run",       ContextifyContext::Run);
        NODE_SET_PROTOTYPE_METHOD(ljsTmpl, "getGlobal", ContextifyContext::GetGlobal);

        NanAssignPersistent(jsTmpl, ljsTmpl);
        target->Set(NanNew("ContextifyContext"), ljsTmpl->GetFunction());
    }

    static NAN_METHOD(New) {
        NanScope();

        if (args.Length() < 1) {
            NanThrowError("Wrong number of arguments passed to ContextifyContext constructor");
            NanReturnUndefined();
        }

        if (!args[0]->IsObject()) {
            NanThrowTypeError("Argument to ContextifyContext constructor must be an object.");
            NanReturnUndefined();
        }

        ContextifyContext* ctx = new ContextifyContext(args[0]->ToObject());
        ctx->Wrap(args.This());
        NanReturnValue(args.This());
    }

    static NAN_METHOD(Run) {
        NanScope();
        if (args.Length() == 0) {
            NanThrowError("Must supply at least 1 argument to run");
        }
        if (!args[0]->IsString()) {
            NanThrowTypeError("First argument to run must be a String.");
            NanReturnUndefined();
        }
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(args.This());
        Local<Context> lcontext = NanNew(ctx->context);
        lcontext->Enter();
        Local<String> code = args[0]->ToString();

        TryCatch trycatch;
        Local<NanBoundScript> script;

        if (args.Length() > 1 && args[1]->IsString()) {
            ScriptOrigin origin(args[1]->ToString());
            script = NanCompileScript(code, origin);
        } else {
            script = NanCompileScript(code);
        }

        if (script.IsEmpty()) {
          lcontext->Exit();
          NanReturnValue(trycatch.ReThrow());
        }

        Handle<Value> result = NanRunScript(script);
        lcontext->Exit();

        if (result.IsEmpty()) {
            NanReturnValue(trycatch.ReThrow());
        }

        NanReturnValue(result);
    }

    static bool InstanceOf(Handle<Value> value) {
      return NanHasInstance(jsTmpl, value);
    }

    static NAN_METHOD(GetGlobal) {
        NanScope();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(args.This());
        NanReturnValue(ctx->proxyGlobal);
    }

    static bool GlobalPropertyNamedAccessCheck(Local<Object> host,
                                               Local<Value>  key,
                                               AccessType    type,
                                               Local<Value>  data) {
        return true;
    }

    static bool GlobalPropertyIndexedAccessCheck(Local<Object> host,
                                                 uint32_t      key,
                                                 AccessType    type,
                                                 Local<Value>  data) {
        return true;
    }

    static NAN_PROPERTY_GETTER(GlobalPropertyGetter) {
        NanScope();
        Local<Object> data = args.Data()->ToObject();
        Local<ContextifyContext> ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        if (!*ctx || ctx->alive != *ctx)
            NanReturnUndefined();

        Local<Object> sandbox = NanNew(ctx->sandbox);
        if (!*sandbox)
            NanReturnUndefined();

        Local<Value> rv = sandbox->GetRealNamedProperty(property);
        if (rv.IsEmpty()) {
            Local<Object> proxyGlobal = NanNew(ctx->proxyGlobal);
            if (!*proxyGlobal)
                NanReturnUndefined();

            rv = proxyGlobal->GetRealNamedProperty(property);
        }
        NanReturnValue(rv);
    }

    static NAN_PROPERTY_SETTER(GlobalPropertySetter) {
        NanScope();
        Local<Object> data = args.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        NanNew(ctx->sandbox)->Set(property, value);
        NanReturnValue(value);
    }

    static NAN_PROPERTY_QUERY(GlobalPropertyQuery) {
        NanScope();
        Local<Object> data = args.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        if (!NanNew(ctx->sandbox)->GetRealNamedProperty(property).IsEmpty() ||
            !NanNew(ctx->proxyGlobal)->GetRealNamedProperty(property).IsEmpty()) {
            NanReturnValue(NanNew<Integer>(None));
         } else {
            NanReturnValue(Handle<Integer>());
         }
    }

    static NAN_PROPERTY_DELETER(GlobalPropertyDeleter) {
        NanScope();
        Local<Object> data = args.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        bool success = NanNew(ctx->sandbox)->Delete(property);
        NanReturnValue(NanNew<Boolean>(success));
    }

    static NAN_PROPERTY_ENUMERATOR(GlobalPropertyEnumerator) {
        NanScope();
        Local<Object> data = args.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        NanReturnValue(NanNew(ctx->sandbox)->GetPropertyNames());
    }
};

class ContextifyScript : public ObjectWrap {
public:
    static Persistent<FunctionTemplate> scriptTmpl;
    Persistent<NanUnboundScript> script;

    static void Init(Handle<Object> target) {
        NanScope();
        Local<FunctionTemplate> lscriptTmpl = NanNew<FunctionTemplate>(New);
        lscriptTmpl->InstanceTemplate()->SetInternalFieldCount(1);
        lscriptTmpl->SetClassName(NanNew("ContextifyScript"));

        NODE_SET_PROTOTYPE_METHOD(lscriptTmpl, "runInContext", RunInContext);

        NanAssignPersistent(scriptTmpl, lscriptTmpl);
        target->Set(NanNew("ContextifyScript"),
                    lscriptTmpl->GetFunction());
    }
    static NAN_METHOD(New) {
        NanScope();
        ContextifyScript *contextify_script = new ContextifyScript();
        contextify_script->Wrap(args.Holder());

        if (args.Length() < 1) {
          NanThrowTypeError("needs at least 'code' argument.");
          NanReturnUndefined();
        }

        Local<String> code = args[0]->ToString();
        Local<String> filename = args.Length() > 1
                               ? args[1]->ToString()
                               : NanNew<String>("ContextifyScript.<anonymous>");

        Handle<Context> context = NanGetCurrentContext();
        Context::Scope context_scope(context);

        // Catch errors
        TryCatch trycatch;

        ScriptOrigin origin(filename);
        Handle<NanUnboundScript> v8_script = NanNew<NanUnboundScript>(code, origin);

        if (v8_script.IsEmpty()) {
          NanReturnValue(trycatch.ReThrow());
        }

        NanAssignPersistent(contextify_script->script, v8_script);

        NanReturnValue(args.This());
    }

    static NAN_METHOD(RunInContext) {
        NanScope();
        if (args.Length() == 0) {
            NanThrowError("Must supply at least 1 argument to runInContext");
            NanReturnUndefined();
        }
        if (!ContextifyContext::InstanceOf(args[0]->ToObject())) {
            NanThrowTypeError("First argument must be a ContextifyContext.");
            NanReturnUndefined();
        }

        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(args[0]->ToObject());
        Local<Context> lcontext = NanNew(ctx->context);
        lcontext->Enter();
        ContextifyScript* wrapped_script = ObjectWrap::Unwrap<ContextifyScript>(args.This());
        Local<NanUnboundScript> script = NanNew(wrapped_script->script);
        TryCatch trycatch;
        if (script.IsEmpty()) {
          lcontext->Exit();
          NanReturnValue(trycatch.ReThrow());
        }
        Handle<Value> result = NanRunScript(script);
        lcontext->Exit();
        if (result.IsEmpty()) {
            NanReturnValue(trycatch.ReThrow());
        }
        NanReturnValue(result);
    }

    ~ContextifyScript() {
        NanDisposePersistent(script);
    }
};

Persistent<FunctionTemplate> ContextifyContext::jsTmpl;
Persistent<FunctionTemplate> ContextifyScript::scriptTmpl;

extern "C" {
    static void init(Handle<Object> target) {
        ContextifyContext::Init(target);
        ContextifyScript::Init(target);
    }
    NODE_MODULE(contextify, init);
};
