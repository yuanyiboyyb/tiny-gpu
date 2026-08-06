#include "tinygpu/compiler/assembler.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "tinygpu/compiler/isa.h"

namespace tinygpu::compiler {
namespace {

struct SourceLine {
    std::size_t number = 0;
    std::string text;
};

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::string uppercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

void diagnose(std::vector<std::string> *diagnostics, std::size_t line,
              const std::string &message)
{
    if (diagnostics) {
        diagnostics->push_back("line " + std::to_string(line) + ": " + message);
    }
}

std::vector<std::string> operands(std::string text)
{
    std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream stream(text);
    std::vector<std::string> result;
    for (std::string word; stream >> word;) {
        result.push_back(std::move(word));
    }
    return result;
}

bool parse_number(std::string text, std::uint32_t &value)
{
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.empty() || text.front() == '-') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 0);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed > 0xfffffffful) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_register(const std::string &text, std::uint8_t &reg)
{
    const std::string name = uppercase(text);
    if (name == "%BLOCKIDX") {
        reg = 13;
        return true;
    }
    if (name == "%BLOCKDIM") {
        reg = 14;
        return true;
    }
    if (name == "%THREADIDX") {
        reg = 15;
        return true;
    }
    if (name.size() < 2 || name.front() != 'R') {
        return false;
    }
    std::uint32_t number = 0;
    if (!parse_number(name.substr(1), number) || number > 15) {
        return false;
    }
    reg = static_cast<std::uint8_t>(number);
    return true;
}

std::vector<SourceLine> source_lines(const std::string &source)
{
    std::vector<SourceLine> result;
    std::istringstream stream(source);
    std::string line;
    for (std::size_t number = 1; std::getline(stream, line); ++number) {
        const auto comment = line.find(';');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (!line.empty()) {
            result.push_back({number, std::move(line)});
        }
    }
    return result;
}

bool strip_labels(SourceLine &line,
                  std::unordered_map<std::string, std::uint8_t> &labels,
                  std::size_t instruction_index,
                  std::vector<std::string> *diagnostics)
{
    while (true) {
        const auto colon = line.text.find(':');
        if (colon == std::string::npos) {
            return true;
        }
        const auto whitespace = line.text.find_first_of(" \t");
        if (whitespace != std::string::npos && whitespace < colon) {
            return true;
        }
        const std::string label = uppercase(trim(line.text.substr(0, colon)));
        if (label.empty()) {
            diagnose(diagnostics, line.number, "empty label");
            return false;
        }
        const std::size_t byte_address = instruction_index * 2;
        if (byte_address > 255) {
            diagnose(diagnostics, line.number, "label address exceeds 8-bit program address space");
            return false;
        }
        if (!labels.emplace(label, static_cast<std::uint8_t>(byte_address)).second) {
            diagnose(diagnostics, line.number, "duplicate label '" + label + "'");
            return false;
        }
        line.text = trim(line.text.substr(colon + 1));
        if (line.text.empty()) {
            return true;
        }
    }
}

bool is_directive(const std::string &line)
{
    return !line.empty() && line.front() == '.';
}

bool parse_directive(const SourceLine &line, AssemblyResult &result,
                     std::vector<std::string> *diagnostics)
{
    auto words = operands(line.text);
    const std::string directive = uppercase(words.front());
    if (directive == ".THREADS") {
        std::uint32_t value = 0;
        if (words.size() != 2 || !parse_number(words[1], value) || value == 0) {
            diagnose(diagnostics, line.number, "expected '.threads <positive-number>'");
            return false;
        }
        result.thread_count = value;
        return true;
    }
    if (directive == ".DATA") {
        for (std::size_t i = 1; i < words.size(); ++i) {
            std::uint32_t value = 0;
            if (!parse_number(words[i], value) || value > 255) {
                diagnose(diagnostics, line.number, "data value must fit in u8");
                return false;
            }
            result.data.push_back(static_cast<std::uint8_t>(value));
        }
        return true;
    }
    diagnose(diagnostics, line.number, "unknown directive '" + words.front() + "'");
    return false;
}

bool branch_mask(const std::string &mnemonic, std::uint8_t &mask)
{
    if (mnemonic == "BR") {
        mask = 7;
        return true;
    }
    if (mnemonic.rfind("BR", 0) != 0 || mnemonic.size() == 2) {
        return false;
    }
    mask = 0;
    for (std::size_t i = 2; i < mnemonic.size(); ++i) {
        switch (mnemonic[i]) {
        case 'N': mask |= 4; break;
        case 'Z': mask |= 2; break;
        case 'P': mask |= 1; break;
        default: return false;
        }
    }
    return mask != 0;
}

bool encode_instruction(const SourceLine &line,
                        const std::unordered_map<std::string, std::uint8_t> &labels,
                        std::uint16_t &encoded,
                        std::vector<std::string> *diagnostics)
{
    auto words = operands(line.text);
    const std::string mnemonic = uppercase(words.front());
    auto bad_operands = [&] {
        diagnose(diagnostics, line.number, "invalid operands for " + mnemonic);
        return false;
    };
    auto reg_at = [&](std::size_t index, std::uint8_t &reg) {
        return index < words.size() && parse_register(words[index], reg);
    };

    if (mnemonic == "NOP" || mnemonic == "JOIN" || mnemonic == "RET") {
        if (words.size() != 1) {
            return bad_operands();
        }
        const Opcode opcode = mnemonic == "NOP" ? Opcode::Nop :
                              mnemonic == "JOIN" ? Opcode::Join : Opcode::Ret;
        encoded = encode_rrr(opcode, 0, 0, 0);
        return true;
    }

    std::uint8_t nzp = 0;
    if (branch_mask(mnemonic, nzp)) {
        if (words.size() != 2) {
            return bad_operands();
        }
        std::uint32_t address = 0;
        const auto label = labels.find(uppercase(words[1]));
        if (label != labels.end()) {
            address = label->second;
        } else if (!parse_number(words[1], address) || address > 255) {
            diagnose(diagnostics, line.number, "unknown label or invalid branch address '" + words[1] + "'");
            return false;
        }
        if ((address & 1u) != 0) {
            diagnose(diagnostics, line.number, "branch address must be 2-byte aligned");
            return false;
        }
        encoded = encode_branch(nzp, static_cast<std::uint8_t>(address));
        return true;
    }

    if (mnemonic == "CONST") {
        std::uint8_t rd = 0;
        std::uint32_t immediate = 0;
        if (words.size() != 3 || !reg_at(1, rd) ||
            !parse_number(words[2], immediate) || immediate > 255 || rd > 12) {
            return bad_operands();
        }
        encoded = encode_const(rd, static_cast<std::uint8_t>(immediate));
        return true;
    }

    if (mnemonic == "CMP") {
        std::uint8_t rs = 0, rt = 0;
        if (words.size() != 3 || !reg_at(1, rs) || !reg_at(2, rt)) {
            return bad_operands();
        }
        encoded = encode_rrr(Opcode::Cmp, 0, rs, rt);
        return true;
    }

    if (mnemonic == "LDR") {
        std::uint8_t rd = 0, rs = 0;
        if (words.size() != 3 || !reg_at(1, rd) || !reg_at(2, rs) || rd > 12) {
            return bad_operands();
        }
        encoded = encode_rrr(Opcode::Ldr, rd, rs, 0);
        return true;
    }

    if (mnemonic == "STR") {
        std::uint8_t rs = 0, rt = 0;
        if (words.size() != 3 || !reg_at(1, rs) || !reg_at(2, rt)) {
            return bad_operands();
        }
        encoded = encode_rrr(Opcode::Str, 0, rs, rt);
        return true;
    }

    const std::unordered_map<std::string, Opcode> arithmetic = {
        {"ADD", Opcode::Add}, {"SUB", Opcode::Sub},
        {"MUL", Opcode::Mul}, {"DIV", Opcode::Div},
    };
    const auto operation = arithmetic.find(mnemonic);
    if (operation != arithmetic.end()) {
        std::uint8_t rd = 0, rs = 0, rt = 0;
        if (words.size() != 4 || !reg_at(1, rd) || !reg_at(2, rs) ||
            !reg_at(3, rt) || rd > 12) {
            return bad_operands();
        }
        encoded = encode_rrr(operation->second, rd, rs, rt);
        return true;
    }

    diagnose(diagnostics, line.number, "unknown instruction '" + words.front() + "'");
    return false;
}

}  // namespace

std::vector<std::uint8_t> AssemblyResult::program_bytes() const
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(instructions.size() * 2);
    for (const std::uint16_t instruction : instructions) {
        bytes.push_back(static_cast<std::uint8_t>(instruction & 0xffu));
        bytes.push_back(static_cast<std::uint8_t>(instruction >> 8));
    }
    return bytes;
}

AssemblyResult assemble(const std::string &source,
                        std::vector<std::string> *diagnostics)
{
    if (diagnostics) {
        diagnostics->clear();
    }
    AssemblyResult result;
    auto lines = source_lines(source);
    std::unordered_map<std::string, std::uint8_t> labels;
    std::size_t instruction_count = 0;

    for (auto &line : lines) {
        if (!strip_labels(line, labels, instruction_count, diagnostics)) {
            continue;
        }
        if (!line.text.empty() && !is_directive(line.text)) {
            ++instruction_count;
            if (instruction_count > 128) {
                diagnose(diagnostics, line.number, "program exceeds 256-byte program memory");
            }
        }
    }

    for (const auto &line : lines) {
        if (line.text.empty()) {
            continue;
        }
        if (is_directive(line.text)) {
            parse_directive(line, result, diagnostics);
            continue;
        }
        std::uint16_t instruction = 0;
        if (encode_instruction(line, labels, instruction, diagnostics)) {
            result.instructions.push_back(instruction);
        }
    }
    return result;
}

}  // namespace tinygpu::compiler
