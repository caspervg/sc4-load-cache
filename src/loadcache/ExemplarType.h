// The ExemplarType (property 0x10) of an exemplar, read from the bytes of its record without
// building the exemplar: a walk over the record the way cSCResExemplarFactory::SerialRead
// (0x0067f1e0) reads it. See docs/re-notes.md, "ExemplarType from the raw record".
//
// The answer is only given when it is certain: if the game loads this record at all, the type it
// reads is the one returned. Nothing is returned when the record has no property 0x10 of its
// own (the game then looks in the parent cohort), when that property is not a single Uint32,
// or for anything this walk does not read exactly as the game does. The caller then lets the
// game load the exemplar.

#pragma once

#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>

namespace exemplar_type
{
    constexpr uint32_t kTypeProperty = 0x10;

    namespace detail
    {
        struct Span
        {
            const uint8_t* p;
            const uint8_t* end;

            size_t Size() const { return static_cast<size_t>(end - p); }
            bool U32(uint32_t& value)
            {
                if (Size() < 4) {
                    return false;
                }
                std::memcpy(&value, p, 4);
                p += 4;
                return true;
            }
            bool Skip(const uint64_t count)
            {
                if (Size() < count) {
                    return false;
                }
                p += static_cast<size_t>(count);
                return true;
            }
        };

        // Bytes of one value by variant code (cGZPersistDBSerialRecord::GetFieldVariant,
        // 0x0097621f): codes 0 to 0xc are single values, 0x8000 + n arrays with a count in front.
        // 0 in the array table: not an array code, or the string (0x800c).
        constexpr uint8_t kSingle[] = {0, 1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 1, 1};
        constexpr uint8_t kArrayElement[] = {1, 1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 1, 0, 0, 0, 0, 2, 1};
        constexpr uint32_t kUint32 = 3, kString = 0x800c;

        // "EQZB1###", parent cohort key, property count, then per property: id, a byte (0: a
        // value, 1: a resource key), and the value as a variant.
        inline std::optional<uint32_t> FromBinary(Span r)
        {
            uint32_t count = 0;
            if (!r.Skip(12) || !r.U32(count)) {
                return std::nullopt;
            }
            // A later property with the same id replaces the earlier one (0x005ff6d0).
            std::optional<uint32_t> type;
            for (; count != 0; --count) {
                uint32_t id = 0, code = 0;
                if (!r.U32(id) || r.Size() < 1) {
                    return std::nullopt;
                }
                const uint8_t kind = *r.p++;
                if (kind == 1 && id != kTypeProperty) {
                    if (!r.Skip(12)) {
                        return std::nullopt;
                    }
                    continue;
                }
                if (kind != 0 || !r.U32(code)) {
                    return std::nullopt;
                }
                if (id == kTypeProperty) {
                    uint32_t value = 0;
                    if (code != kUint32 || !r.U32(value)) {
                        return std::nullopt;
                    }
                    type = value;
                }
                else if (code < std::size(kSingle)) {
                    if (!r.Skip(kSingle[code])) {
                        return std::nullopt;
                    }
                }
                else if (code == kString) {
                    // GetFieldString (0x00977054): a length with 0 in its top byte, at most
                    // 200000. It reads other lengths in ways not followed here.
                    uint32_t length = 0;
                    if (!r.U32(length) || length > 200000 || !r.Skip(length)) {
                        return std::nullopt;
                    }
                }
                else {
                    const uint32_t n = code - 0x8000;
                    uint32_t values = 0;
                    if (n >= std::size(kArrayElement) || kArrayElement[n] == 0 || !r.U32(values) ||
                        !r.Skip(static_cast<uint64_t>(values) * kArrayElement[n])) {
                        return std::nullopt;
                    }
                }
            }
            return type;
        }

        // The game's number reader for text exemplars (0x0090fe93): strtoul, in base 16 after
        // "0x" or when a hex letter comes before the first character that is no hex digit, else
        // in base 10. Nothing for a first character strtoul would skip or take as a sign.
        inline std::optional<uint32_t> ToUint(Span s)
        {
            const auto digit = [](const uint8_t c) -> uint32_t {
                return c >= '0' && c <= '9'   ? c - '0'
                       : c >= 'a' && c <= 'f' ? c - 'a' + 10
                       : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                              : 99;
            };
            if (s.p == s.end) {
                return 0;
            }
            if (*s.p <= ' ' || *s.p >= 0x80 || *s.p == '+' || *s.p == '-') {
                return std::nullopt;
            }
            uint32_t radix = 10;
            if (s.Size() >= 2 && s.p[0] == '0' && (s.p[1] == 'x' || s.p[1] == 'X')) {
                radix = 16;
                s.p += 2;
            }
            else {
                for (const uint8_t* c = s.p; c != s.end && digit(*c) < 16; ++c) {
                    if (digit(*c) >= 10) {
                        radix = 16;
                        break;
                    }
                }
            }
            uint32_t value = 0;
            bool overflow = false;
            for (; s.p != s.end && digit(*s.p) < radix; ++s.p) {
                if (value > (0xFFFFFFFFu - digit(*s.p)) / radix) {
                    overflow = true;
                }
                else {
                    value = value * radix + digit(*s.p);
                }
            }
            return overflow ? 0xFFFFFFFFu : value;
        }

        // The next line that is not empty (0x0067c240), split at its first '=' with spaces and
        // tabs taken off both parts (0x0067e0b0).
        inline bool Entry(Span& text, Span& name, Span& value)
        {
            const auto trim = [](Span s) {
                while (s.p != s.end && (*s.p == ' ' || *s.p == '\t')) {
                    ++s.p;
                }
                while (s.p != s.end && (s.end[-1] == ' ' || s.end[-1] == '\t')) {
                    --s.end;
                }
                return s;
            };
            while (text.p != text.end) {
                const auto* newline = static_cast<const uint8_t*>(std::memchr(text.p, '\n', text.Size()));
                Span line{text.p, newline ? newline : text.end};
                text.p = newline ? newline + 1 : text.end;
                if (line.p != line.end && line.end[-1] == '\r') {
                    --line.end;
                }
                if (line.p == line.end) {
                    continue;
                }
                const auto* equals = static_cast<const uint8_t*>(std::memchr(line.p, '=', line.Size()));
                if (!equals) {
                    return false;
                }
                name = trim({line.p, equals});
                value = trim({equals + 1, line.end});
                return true;
            }
            return false;
        }

        inline bool StartsWith(const Span s, const char* literal)
        {
            const size_t length = std::strlen(literal);
            return s.Size() >= length && std::memcmp(s.p, literal, length) == 0;
        }

        // The names of the first two entries are compared without case (_stricmp).
        inline bool IsName(const Span s, const char* literal)
        {
            if (s.Size() != std::strlen(literal)) {
                return false;
            }
            for (size_t i = 0; i < s.Size(); ++i) {
                const uint8_t a = s.p[i], b = static_cast<uint8_t>(literal[i]);
                if ((a >= 'A' && a <= 'Z' ? a + 32 : a) != (b >= 'A' && b <= 'Z' ? b + 32 : b)) {
                    return false;
                }
            }
            return true;
        }

        // "EQZT1###\r\n", then lines: ParentCohort=Key:{...}, PropCount=n, and n properties such
        // as 0x00000010:{"Exemplar Type"}=Uint32:0:{0x00000021}.
        inline std::optional<uint32_t> FromText(Span text)
        {
            if (std::memchr(text.p, 0, text.Size())) {
                return std::nullopt;  // the game's strings would end there
            }
            Span name{}, value{};
            if (!Entry(text, name, value) || !IsName(name, "ParentCohort") || !Entry(text, name, value) ||
                !IsName(name, "PropCount")) {
                return std::nullopt;
            }
            const std::optional<uint32_t> count = ToUint(value);
            if (!count) {
                return std::nullopt;
            }
            std::optional<uint32_t> type;
            for (uint32_t i = 0; i < *count; ++i) {
                if (!Entry(text, name, value)) {
                    return std::nullopt;
                }
                const std::optional<uint32_t> id = ToUint(name);
                if (!id) {
                    return std::nullopt;
                }
                if (*id != kTypeProperty) {
                    continue;
                }
                // The value reader (0x0067e780): type name up to ':', count up to the next
                // ':', then '{' and the values. Only a single Uint32 is read here.
                if (!StartsWith(value, "Uint32:")) {
                    return std::nullopt;
                }
                value.p += 7;
                const auto* colon = static_cast<const uint8_t*>(std::memchr(value.p, ':', value.Size()));
                if (ToUint(value) != 0u || !colon || colon + 1 == value.end || colon[1] != '{') {
                    return std::nullopt;
                }
                type = ToUint({colon + 2, value.end});
                if (!type) {
                    return std::nullopt;
                }
            }
            return type;
        }
    }

    inline std::optional<uint32_t> FromRecord(const uint8_t* data, const size_t size)
    {
        // The game compares the first four bytes only, then skips the rest of the header.
        if (size >= 8 && std::memcmp(data, "EQZB", 4) == 0) {
            return detail::FromBinary({data + 8, data + size});
        }
        if (size >= 10 && std::memcmp(data, "EQZT", 4) == 0) {
            return detail::FromText({data + 10, data + size});
        }
        return std::nullopt;
    }
}
