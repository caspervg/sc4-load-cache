// Checks exemplar_type::FromRecord: a few hand-made records, and with a file argument every
// record of a dump made by check_exemplar_types.py (key, length, bytes), one result per line.

#include <cstdio>
#include <string>
#include <vector>

#include "ExemplarType.h"

namespace
{
    using namespace std::string_literals;

    int gFailed = 0;

    void Check(const char* name, const std::string& record, const std::optional<uint32_t> expected)
    {
        const auto got = exemplar_type::FromRecord(reinterpret_cast<const uint8_t*>(record.data()), record.size());
        if (got != expected) {
            std::printf("FAILED %s: got %s%08X\n", name, got ? "" : "nothing, not ", got.value_or(0));
            ++gFailed;
        }
    }

    std::string U32(const uint32_t value) { return std::string(reinterpret_cast<const char*>(&value), 4); }
    // One property: id, the value byte, a variant code and its bytes.
    std::string Property(const uint32_t id, const uint32_t code, const std::string& bytes)
    {
        return U32(id) + '\0' + U32(code) + bytes;
    }
    std::string Binary(const uint32_t count, const std::string& properties)
    {
        return "EQZB1###" + std::string(12, '\0') + U32(count) + properties;
    }
    std::string Text(const std::string& lines) { return "EQZT1###\r\n" + lines; }

    void BuiltIn()
    {
        const std::string type21 = Property(0x10, 3, U32(0x21));
        const std::string name = Property(0x20, 0x800c, U32(3) + "abc");
        const std::string floats = Property(0x27812810, 0x8009, U32(2) + std::string(8, '\0'));
        Check("one property", Binary(1, type21), 0x21);
        Check("after others", Binary(3, name + floats + type21), 0x21);
        Check("the last one wins", Binary(3, type21 + name + Property(0x10, 3, U32(2))), 2);
        Check("bytes after the last property", Binary(1, type21) + "junk", 0x21);
        Check("no type", Binary(2, name + floats), std::nullopt);
        Check("no properties", Binary(0, ""), std::nullopt);
        Check("an array of one", Binary(1, Property(0x10, 0x8003, U32(1) + U32(0x21))), std::nullopt);
        Check("a Uint8", Binary(1, Property(0x10, 1, "\x21")), std::nullopt);
        Check("a later array", Binary(2, type21 + Property(0x10, 0x8003, U32(1) + U32(2))), std::nullopt);
        Check("count too high", Binary(2, type21), std::nullopt);
        Check("cut off in a value", Binary(2, type21 + Property(0x20, 0x800c, U32(9) + "abc")), std::nullopt);
        Check("unknown variant code", Binary(2, type21 + Property(0x30, 0x0d, "")), std::nullopt);
        Check("unknown array code", Binary(2, type21 + Property(0x30, 0x800d, U32(0))), std::nullopt);
        Check("unknown kind", Binary(2, type21 + U32(0x30) + '\2' + U32(3) + U32(0)), std::nullopt);
        Check("a resource key property", Binary(2, U32(0x30) + '\1' + std::string(12, '\1') + type21), 0x21);
        Check("the type as a resource key", Binary(1, U32(0x10) + '\1' + std::string(12, '\1')), std::nullopt);
        Check("a wide string", Binary(2, type21 + Property(0x20, 0x800c, U32(0x10000001) + "a\0"s)), std::nullopt);
        Check("too short", "EQZB1##", std::nullopt);
        Check("a cohort", "CQZB1###" + Binary(1, type21).substr(8), std::nullopt);
        Check("only the first four bytes count", "EQZBxxxx" + Binary(1, type21).substr(8), 0x21);

        const std::string head = "ParentCohort=Key:{0x00000000,0x00000000,0x00000000}\r\nPropCount=0x00000002\r\n";
        const std::string textName = "0x00000020:{\"Exemplar Name\"}=String:1:{\"a=b\"}\r\n";
        Check("text", Text(head + "0x00000010:{\"Exemplar Type\"}=Uint32:0:{0x00000021}\r\n" + textName), 0x21);
        Check("text, last line without an end", Text(head + textName + "0x00000010:{\"x\"}=Uint32:0:{0x0000000F}"), 0xf);
        Check("text, decimal and blanks", Text("\r\n parentcohort = Key:{}\n\nPROPCOUNT=1\n16 = Uint32:0:{33}\n"), 33);
        Check("text, hex without 0x", Text("ParentCohort=x\nPropCount=1\n0010:{\"t\"}=Uint32:0:{1f}\n"), std::nullopt);
        Check("text, hex letters make base 16", Text("ParentCohort=x\nPropCount=1\n1a=Uint32:0:{1}\n10=Uint32:0:{1f}\n"),
              std::nullopt);
        Check("text, a hex letter in the value", Text("ParentCohort=x\nPropCount=2\n1a=Uint32:0:{1}\n16=Uint32:0:{1f}\n"),
              0x1f);
        Check("text, count below the lines", Text(head.substr(0, head.size() - 3) + "1\r\n" + textName +
                                                  "0x00000010:{\"Exemplar Type\"}=Uint32:0:{0x00000021}\r\n"),
              std::nullopt);
        Check("text, count above the lines", Text(head + "0x00000010:{\"Exemplar Type\"}=Uint32:0:{0x00000021}\r\n"),
              std::nullopt);
        Check("text, an array", Text(head + textName + "0x00000010:{\"x\"}=Uint32:1:{0x00000021}\r\n"), std::nullopt);
        Check("text, a Sint32", Text(head + textName + "0x00000010:{\"x\"}=Sint32:0:{0x00000021}\r\n"), std::nullopt);
        Check("text, a blank before the value", Text(head + textName + "0x00000010:{\"x\"}=Uint32:0:{ 0x21}\r\n"),
              std::nullopt);
        Check("text, no brace", Text(head + textName + "0x00000010:{\"x\"}=Uint32:0:0x21\r\n"), std::nullopt);
        Check("text, a line without =", Text(head + "junk\r\n0x00000010:{\"x\"}=Uint32:0:{0x21}\r\n"), std::nullopt);
        Check("text, wrong first entry", Text("PropCount=1\n0x10=Uint32:0:{1}\n"), std::nullopt);
        Check("text, too large a number", Text("ParentCohort=x\nPropCount=1\n0x10=Uint32:0:{0x123456789}\n"), 0xFFFFFFFF);
        Check("text, a zero byte", Text(head + textName + "0x00000010:{\"x\"}=Uint32:0:{0x21}\r\n" + '\0'), std::nullopt);
    }

    int Dump(const char* path)
    {
        FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0 || !file) {
            std::printf("cannot open %s\n", path);
            return 2;
        }
        uint32_t header[4];
        std::vector<uint8_t> bytes;
        while (std::fread(header, 4, 4, file) == 4) {
            bytes.resize(header[3]);
            if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
                std::printf("dump cut off\n");
                return 2;
            }
            const auto type = exemplar_type::FromRecord(bytes.data(), bytes.size());
            if (type) {
                std::printf("%08X %08X %08X %08X\n", header[0], header[1], header[2], *type);
            }
            else {
                std::printf("%08X %08X %08X -\n", header[0], header[1], header[2]);
            }
        }
        std::fclose(file);
        return 0;
    }
}

int main(const int argc, char** argv)
{
    if (argc > 1) {
        return Dump(argv[1]);
    }
    BuiltIn();
    std::printf(gFailed ? "%d checks failed\n" : "all checks passed\n", gFailed);
    return gFailed ? 1 : 0;
}
