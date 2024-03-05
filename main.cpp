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

#include <iostream>
#include <fstream>
#include <filesystem>
#include <print>
#include <vector>

#pragma warning (push)
#pragma warning (disable: 4251)
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#pragma warning (pop)

struct protobuf_data {
    google::protobuf::FileDescriptorProto descriptor;
    std::string definition;
    std::vector<uint8_t> raw_bytes;
};

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

std::vector<uint8_t>::const_iterator find_compiled_protobuf_descriptor_start(
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

std::unordered_map<std::string, protobuf_data> extract_protobuf_descriptors(std::vector<uint8_t> buffer) {

    std::unordered_map<std::string, protobuf_data> ret;

    const std::string pattern_end = "\x62\x06proto3";

    for (std::vector<uint8_t>::const_iterator pos = buffer.cbegin(), end; ; pos = end) {

        end = std::search(pos, buffer.cend(),
            pattern_end.cbegin(), pattern_end.cend());
        if (end == buffer.cend())
            break;

        end += pattern_end.length();

        const auto start = find_compiled_protobuf_descriptor_start(pos, end);
        if (start == end)
            continue;

        google::protobuf::FileDescriptorProto proto;
        proto.ParseFromArray(&*start, (int)(end - start));
        std::println("Found {} in binary file", proto.name());
        ret[proto.name()] = {
            .descriptor = proto,
            .raw_bytes = std::vector<uint8_t>(start, end),
        };
    }
    return ret;
}

void build_protobuf_descriptor(
    const std::filesystem::path& output_directory,
    google::protobuf::DescriptorPool& pool,
    std::unordered_map<std::string, protobuf_data>& protos,
    const std::string& definition_filename,
    std::vector<std::string>& load_order,
    size_t indent = 0
)
{
    protobuf_data& proto_data = protos.at(definition_filename);
    const auto& proto = proto_data.descriptor;
    const std::filesystem::path dir = std::filesystem::path(proto.name()).parent_path();

    if (indent) std::print("{:{}}", "", indent);
    std::println("Loading {} ({} dependencies)", definition_filename, proto.dependency_size());

    for (int i = 0; i < proto.dependency_size(); ++i) {
        const std::string& dependency = proto.dependency(i);
        if (pool.FindFileByName(dependency))
            continue;
        build_protobuf_descriptor(output_directory, pool, protos, dependency, load_order, indent + 3);
    }

    proto_data.definition = pool.BuildFile(proto)->DebugString();
    const std::filesystem::path output_file = output_directory / "proto" / definition_filename;

    // Check if file already exists
    std::ifstream existing(output_file, std::ios::ate);
    if (existing.fail()) {
        // Does not already exist
        std::println(">>> New proto file: {}", definition_filename);
    }
    else {
        // Already exists. Check if it is equal to what we are going to output.
        // We cannot short-circuit this by comparing file sizes, due to different newline formats - \n vs \r\n
        existing.seekg(0, std::ios::beg);
        bool equal = std::equal(
            std::istreambuf_iterator<char>(existing.rdbuf()),
            std::istreambuf_iterator<char>(),
            proto_data.definition.begin());
        existing.close();

        if (!equal) {
            // If not equal, back up old file for reference
            std::println(">>> {} has changed", definition_filename);
            std::filesystem::rename(output_file, output_file.string() + ".old");
        }
    }

    std::filesystem::create_directories(output_directory / "proto" / dir);
    std::ofstream out(output_file);
    out << proto_data.definition;

    const std::string pbName = definition_filename.substr(0, definition_filename.find_last_of('.')) + ".pb";
    load_order.push_back(pbName);

    std::filesystem::create_directories(output_directory / "pb" / dir);
    out = std::ofstream(output_directory / "pb" / pbName, std::ios::binary);
    const auto& raw_bytes = proto_data.raw_bytes;
    std::copy(raw_bytes.cbegin(), raw_bytes.cend(), std::ostream_iterator<uint8_t>(out));

    protos.erase(definition_filename);
}

void build_protobuf_descriptor_pool(
    const std::filesystem::path& output_directory,
    google::protobuf::DescriptorPool& pool,
    std::unordered_map<std::string, protobuf_data>& descriptors,
    std::vector<std::string>& load_order
)
{
    while (!descriptors.empty())
        build_protobuf_descriptor(output_directory, pool, descriptors, descriptors.begin()->first, load_order);
}

void write_json(std::filesystem::path json_filepath, std::vector<std::string>& json_array) {

    std::ofstream out(json_filepath);

    std::print(out, "[");
    for (int i = 0; i < json_array.size(); ++i) {
        if (i)
            std::print(out, ",");
        std::print(out, "\n    \"{}\"", json_array[i]);
    }
    std::print(out, "\n]");
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
    auto descriptors = extract_protobuf_descriptors(binary_file);

    std::println("");

    google::protobuf::DescriptorPool pool;
    std::vector<std::string> load_order;
    build_protobuf_descriptor_pool(output_directory, pool, descriptors, load_order);

    if (argc < 4)
        return 0;

    const std::string json_filename(args[3]);
    write_json(output_directory / json_filename, load_order);
}