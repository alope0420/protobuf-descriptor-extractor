/*
Copyright (c) 2024 Alexander Rød Opedal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "ProtobufResolver.hpp"

std::vector<uint8_t> read_binary_file(const std::string filename)
{
    std::ifstream file(filename, std::ios::binary);
    file.unsetf(std::ios::skipws);

    file.seekg(0, std::ios::end);
    std::streampos file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> vec;
    vec.reserve(file_size);
    vec.insert(vec.begin(),
        std::istream_iterator<uint8_t>(file),
        std::istream_iterator<uint8_t>());
    return vec;
}

std::vector<uint8_t>::const_iterator find_compiled_descriptor_start(
    std::vector<uint8_t>::const_iterator buffer_start,
    std::vector<uint8_t>::const_iterator descriptor_end)
{
    // could tack on \\x12 to reduce number of false positives,
    // but at the cost of false negatives if proto lacks package declaration
    const std::string pattern_begin = ".proto";

    const auto rev_buffer_start = std::make_reverse_iterator(buffer_start);

    // Heuristic search for start of protobuf definition. This relies on the fact that compiled protobuf definitions include the name 
    // of the .proto file they are generated from (which should end in ".proto") as the very first field of the descriptor.
    // Given the end position of a probable protobuf descriptor, we iterate in reverse to find the beginning.
    for (auto proto_filename_pos = std::make_reverse_iterator(descriptor_end); ; proto_filename_pos += pattern_begin.length()) {

        // Find next occurrence of ".proto"
        proto_filename_pos = std::search(proto_filename_pos, rev_buffer_start, pattern_begin.crbegin(), pattern_begin.crend());
        if (proto_filename_pos == rev_buffer_start)
            return descriptor_end; // i.e. not found

        // Look for a 0x0a byte, which signifies the tag for the field containing the .proto file name in the protobuf descriptor
        const auto rev_start = std::find(proto_filename_pos, rev_buffer_start, 0x0a);
        if (rev_start == rev_buffer_start)
            return descriptor_end; // i.e. not found

        const auto start = rev_start.base() - 1; // base advances iterator by 1, so subtract 1 to correct

        // Rule out false positives by checking that the byte after 0x0a encodes the file name string length
        // The left hand side of the comparison signifies: start position + 0x0a byte + length byte + proto file name string length
        // TODO: this breaks if length > 127, since we parse a uint8 rather than a varint
        if (start + 2 + *(start + 1) == proto_filename_pos.base())
            return start;
    }
}

std::vector<std::vector<uint8_t>> extract_descriptors(std::vector<uint8_t> buffer) {

    std::vector<std::vector<uint8_t>> ret;

    const std::string pattern_end = "\x62\x06proto3";

    // Loop through buffer looking for proto descriptors - every time we find one,
    // use the end position as the start position for the next iteration
    for (std::vector<uint8_t>::const_iterator pos = buffer.cbegin(), end; ; pos = end) {

        // Start by looking for end of proto definition, as it is more distinct
        end = std::search(pos, buffer.cend(),
            pattern_end.cbegin(), pattern_end.cend());
        if (end == buffer.cend())
            break; //EOF - we're done

        // Add length of end signature to get actual end of definition
        end += pattern_end.length();

        // Now find the corresponding start signature
        const auto start = find_compiled_descriptor_start(pos, end);
        if (start == end)
            continue; //false positive (seems unlikely to happen)

        ret.push_back(std::vector<uint8_t>(start, end));
    }
    return ret;
}

int main(int argc, char* args[]) {

    if (argc < 3) {
        std::println("Usage: {} <input-file> <output-directory> [load-order-json-filename]",
            std::filesystem::path(std::string(args[0])).filename().string());
        return -1;
    }

    const std::string input_file(args[1]);
    const std::filesystem::path output_directory(args[2]);

    auto binary_file = read_binary_file(input_file);
    auto descriptors = extract_descriptors(binary_file);

    ProtobufResolver resolver(descriptors);
    resolver.backup_replaced_dump_files = false; //TODO: add command line switch for this
    std::println("");
    resolver.dumpFiles(output_directory);
    // dump definitions, compileds

    if (argc < 4)
        return 0;

    const std::string json_filename(args[3]);
    std::ofstream out(output_directory / json_filename);
    out << resolver.getLoadOrderAsJson();
}