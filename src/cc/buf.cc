#include <v8.h>
#include <node.h>
#include "buf.hh"

using namespace buf;


#define ASSERT_STRING_LIKE(val)                                             \
    if (!Buf::IsStringLike(val)) {                                          \
        return NanThrowTypeError("requires buf/string/buffer");             \
    }

#define ASSERT_ARGS_LEN(len)                                                \
    if (args.Length() != len) {                                             \
        buf_t *err = buf_new(21);                                           \
        buf_sprintf(err, "takes exactly %d args", len);                     \
        NanThrowError(buf_str(err));                                        \
        buf_free(err);                                                      \
        return;                                                             \
    }                                                                       \

#define ASSERT_ARGS_LEN_GT(len)                                             \
    if (!(args.Length() > len)) {                                           \
        buf_t *err = buf_new(22);                                           \
        buf_sprintf(err, "takes at least %d args", len + 1);                \
        NanThrowError(buf_str(err));                                        \
        buf_free(err);                                                      \
        return;                                                             \
    }                                                                       \

#define ASSERT_ARGS_LEN_LT(len)                                             \
    if (!(args.Length() < len)) {                                           \
        buf_t *err = buf_new(21);                                           \
        buf_sprintf(err, "takes at most %d args", len - 1);                 \
        NanThrowError(buf_str(err));                                        \
        buf_free(err);                                                      \
        return;                                                             \
    }                                                                       \

#define ASSERT_UINT32(val)                                                  \
    if (!val->IsUint32()) {                                                 \
        return NanThrowTypeError("requires unsigned integer");              \
    }

#define ASSERT_INT32(val)                                                  \
    if (!val->IsInt32()) {                                                 \
        return NanThrowTypeError("requires integer");                      \
    }

Persistent<FunctionTemplate> Buf::constructor;

Buf::Buf(size_t unit) {
    buf = buf_new(unit);
}

Buf::~Buf() {
    buf_free(buf);
}

/**
 * Register prototypes and exports
 */
void Buf::Initialize(Handle<Object> exports) {
    NanScope();
    // Constructor
    Local<FunctionTemplate> ctor = NanNew<FunctionTemplate>(New);
    ctor->InstanceTemplate()->SetInternalFieldCount(1);
    ctor->SetClassName(NanNew("Buf"));
    // Persistents
    NanAssignPersistent(constructor, ctor);
    // Accessors
    ctor->InstanceTemplate()->SetAccessor(NanNew<String>("cap"), GetCap, SetCap);
    ctor->InstanceTemplate()->SetAccessor(NanNew<String>("length"), GetLength, SetLength);
    ctor->InstanceTemplate()->SetIndexedPropertyHandler(GetIndex, SetIndex);
    // Prototype
    NODE_SET_PROTOTYPE_METHOD(ctor, "put", Put);
    NODE_SET_PROTOTYPE_METHOD(ctor, "pop", Pop);
    NODE_SET_PROTOTYPE_METHOD(ctor, "clear", Clear);
    NODE_SET_PROTOTYPE_METHOD(ctor, "copy", Copy);
    NODE_SET_PROTOTYPE_METHOD(ctor, "slice", Slice);
    NODE_SET_PROTOTYPE_METHOD(ctor, "inspect", Inspect);
    NODE_SET_PROTOTYPE_METHOD(ctor, "toString", ToString);
    // Class methods
    NODE_SET_METHOD(ctor->GetFunction(), "isBuf", IsBuf);
    // Exports
    exports->Set(NanNew<String>("Buf"), ctor->GetFunction());
}

bool Buf::HasInstance(Handle<Value> val) {
    return val->IsObject() && Buf::HasInstance(val.As<Object>());
}

bool Buf::HasInstance(Handle<Object> obj) {
    return obj->InternalFieldCount() == 1 &&
        NanHasInstance(constructor, obj);
}

bool Buf::IsStringLike(Handle<Value> val) {
    if (val->IsNumber())
        return false;
    return Buf::IsStringLike(val.As<Object>());
}

bool Buf::IsStringLike(Handle<Object> obj) {
    return obj->IsString() || Buffer::HasInstance(obj) ||
        Buf::HasInstance(obj);
}

/**
 * Public API: - new Buf  O(1)
 */
NAN_METHOD(Buf::New) {
    NanScope();
    ASSERT_ARGS_LEN(1);
    ASSERT_UINT32(args[0]);

    if (args.IsConstructCall()) {
        size_t unit = args[0]->Uint32Value();

        if (unit == 0) {
            NanThrowError("buf unit should not be 0");
        } else if (unit > BUF_MAX_UNIT) {
            NanThrowError("buf unit is too large");
        } else {
            Buf *buf = new Buf(unit);
            buf->Wrap(args.This());
            NanReturnValue(args.This());
        }
    } else {
        // turn to construct call
        Local<Value> argv[1] = { args[0] };
        Local<FunctionTemplate> ctor = NanNew<FunctionTemplate>(constructor);
        NanReturnValue(ctor->GetFunction()->NewInstance(1, argv));
    }
}

/**
 * Public API: - Buf.isBuf O(1)
 */
NAN_METHOD(Buf::IsBuf) {
    NanScope();
    ASSERT_ARGS_LEN(1);
    NanReturnValue(NanNew<Boolean>(Buf::HasInstance(args[0])));
}

/**
 * Public API: - buf.cap O(1)
 */
NAN_GETTER(Buf::GetCap) {
    NanScope();
    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());
    NanReturnValue(NanNew<Number>(self->buf->cap));
}

/**
 * Public API: - buf.cap (disabled helper) O(1)
 */
NAN_SETTER(Buf::SetCap) {
    NanScope();
    NanThrowError("cannot set buf.cap");
}

/**
 * Public API: - buf.length O(1)
 */
NAN_GETTER(Buf::GetLength) {
    NanScope();
    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());
    NanReturnValue(NanNew<Number>(self->buf->size));
}

/**
 * Public API: - buf.length O(1)/O(k)
 */
NAN_SETTER(Buf::SetLength) {
    NanScope();
    ASSERT_UINT32(value);

    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());
    buf_t *buf = self->buf;
    size_t len = value->Uint32Value();

    if (len < buf->size) {
        // truncate
        buf_rrm(buf, buf->size - len);
    }

    if (len > buf->size) {
        // append space
        buf_grow(buf, len);

        while (buf->size < len)
            buf_putc(buf, 0x20);
    }

    NanReturnValue(NanNew<Number>(buf->size));
}

/**
 * Public API: - buf[idx] O(1)
 */
NAN_INDEX_GETTER(Buf::GetIndex) {
    NanScope();

    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());

    if (index >= self->buf->size) {
        NanReturnUndefined();
    } else {
        char s[1] = {0};
        s[0] = (self->buf->data)[index];
        NanReturnValue(NanNew<String>(s));
    }
}

/**
 * Public API: - buf[idx] = 'c' O(1)
 */
NAN_INDEX_SETTER(Buf::SetIndex) {
    NanScope();
    ASSERT_STRING_LIKE(value);

    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());

    if (index >= self->buf->size) {
        NanReturnValue(NanNew<Boolean>(false));
    } else {
        Local<String> val = value->ToString();

        if (val->Length() != 1) {
            NanThrowError("requires a single char");
        } else {
            String::Utf8Value tmp(val);
            char *s = *tmp;
            (self->buf->data)[index] = s[0];
            NanReturnValue(NanNew<String>(s));
        }
    }
}

/**
 * Public API: - Buf.prototype.put O(k)
 */
NAN_METHOD(Buf::Put) {
    NanScope();
    ASSERT_ARGS_LEN(1);
    ASSERT_STRING_LIKE(args[0]);

    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());
    String::Utf8Value tmp(args[0]->ToString());
    char *val = *tmp;
    size_t size = self->buf->size;
    int result = buf_puts(self->buf, val);

    switch(result) {
        case BUF_OK:
            NanReturnValue(NanNew<Number>(self->buf->size - size));
            break;
        case BUF_ENOMEM:
            NanThrowError("No memory");
            break;
    }
}

/**
 * Public APi: - Buf.prototype.pop O(1)
 */
NAN_METHOD(Buf::Pop) {
    NanScope();
    ASSERT_ARGS_LEN(1);
    ASSERT_UINT32(args[0]);

    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());
    NanReturnValue(NanNew<Number>(
                buf_rrm(self->buf, args[0]->Uint32Value())));
}

/**
 * Public API: - Buf.prototype.toString  O(1)
 */
NAN_METHOD(Buf::ToString) {
    NanScope();
    ASSERT_ARGS_LEN(0);

    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());
    NanReturnValue(NanNew<String>(buf_str(self->buf)));
}

/**
 * Public API: - Buf.prototype.clear O(1)
 */
NAN_METHOD(Buf::Clear) {
    NanScope();
    ASSERT_ARGS_LEN(0);

    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());
    size_t size = self->buf->size;
    buf_clear(self->buf);
    NanReturnValue(NanNew<Number>(size));
}

/**
 * Public API: - Buf.prototype.inspect
 */
NAN_METHOD(Buf::Inspect) {
    NanScope();
    Buf *self = ObjectWrap::Unwrap<Buf>(args.Holder());
    buf_t *buf = buf_new(self->buf->size + 12);  // ensure not 0
    buf_sprintf(buf, "<buf [%d] '%.10s%s'>", self->buf->size,
            buf_str(self->buf), self->buf->size > 10 ? ".." : "");
    Local<Value> val = NanNew<String>(buf_str(buf));
    buf_free(buf);
    NanReturnValue(val);
}

/**
 * Public API: - Buf.prototype.copy O(n)
 */
NAN_METHOD(Buf::Copy) {
    NanScope();
    Buf *self = ObjectWrap::Unwrap<Buf>(args.This());
    Local<Value> argv[1] = { NanNew<Number>(self->buf->unit) };
    Local<FunctionTemplate> ctor = NanNew<FunctionTemplate>(constructor);
    Local<Object> inst = ctor->GetFunction()->NewInstance(1, argv);
    Buf *copy = ObjectWrap::Unwrap<Buf>(inst);
    buf_put(copy->buf, self->buf->data, self->buf->size);
    NanReturnValue(inst);
}

/**
 * Public API: - Buf.prototype.slice O(k)
 */
NAN_METHOD(Buf::Slice) {
    NanScope();
    ASSERT_ARGS_LEN_GT(0);
    ASSERT_ARGS_LEN_LT(3);
    ASSERT_INT32(args[0]);

    if (args.Length() > 1)
        ASSERT_INT32(args[1]);

    Buf *self = ObjectWrap::Unwrap<Buf>(args.This());

    // make a copy
    Local<Value> argv[1] = { NanNew<Number>(self->buf->unit) };
    Local<FunctionTemplate> ctor = NanNew<FunctionTemplate>(constructor);
    Local<Object> inst = ctor->GetFunction()->NewInstance(1, argv);
    Buf *copy = ObjectWrap::Unwrap<Buf>(inst);

    // slice data

    int begin = args[0]->Int32Value();
    int end;

    size_t len;
    size_t size = self->buf->size;

    if (args.Length() == 1)
        end = size;
    else
        end = args[1]->Int32Value();

    if (begin < 0) begin += size;
    if (begin < 0) begin = 0;

    if (end < 0) end += size;
    if (end > int(size)) end = size;

    if (begin < end) len = end - begin;
    if (begin >= end) len = 0;

    if (len > 0)
        buf_grow(copy->buf, len);

    size_t idx = 0;

    while (idx < len)
        buf_putc(copy->buf, (self->buf->data)[begin + idx++]);
    NanReturnValue(inst);
}
