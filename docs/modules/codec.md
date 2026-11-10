# codec

A tolerant field codec. Namespace `core::codec`, directory `src/core/codec/`, target
`core::codec`. It depends on [base](base.md) (for `core::Flags`) and builds for WebAssembly.

Moved here from contour's `vtbackend` state codec, so that processes which must not link a terminal
engine can speak versioned messages with the same machinery.

| Header | What it has |
|---|---|
| `<core/codec/Stream.hpp>` | `Writer` and `Reader`: LEB128 varints, zigzag signed varints, length-prefixed bytes and strings, frames. No header: a format writes its own magic and version first |
| `<core/codec/Fields.hpp>` | `FieldEncoder` and `FieldDecoder`, `encodeFields()` and `decodeFields()`, `EnumWire` and `FlagWire` (stable wire tables), `DecodeBudget` |
| `<core/codec/DecodeError.hpp>` | `DecodeError`, the failure every read answers |

## A struct lists its fields once

```cpp
struct Hello
{
    uint32_t version = 0;
    std::string name;

    void visitFields(this auto& self, auto& visitor)
    {
        visitor(1, self.version);
        visitor(2, self.name);
    }
};
```

The id is the field's name on the wire: stable, never reused. The encoder and the decoder are the two
visitors of the same list, so they cannot disagree. A reader skips the ids it does not know and leaves a
field the stream lacks at its default, which is the whole compatibility story.

Enumerations and flag sets cross by their position in a table, `EnumWire<E>::Values` and
`FlagWire<F>::Values`, appended to and never reordered. A type that cannot carry a `visitFields` member
is listed by a `visitForeignFields(visitor, value)` overload, found by argument-dependent lookup through
the hooks tag of `BasicFieldEncoder<Hooks>` and `BasicFieldDecoder<Hooks>`, so each project's overloads
stay its own.
