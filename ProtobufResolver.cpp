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

ProtobufResolver::ProtobufResolver(const std::vector<std::vector<uint8_t>> compiled_descriptors) {

    std::unordered_map<std::string, protobuf_data> descriptors;
    descriptors.reserve(compiled_descriptors.size());

    for (const auto& compiled_descriptor : compiled_descriptors) {
        protobuf_data data = { .compiled = compiled_descriptor };
        data.descriptor.ParseFromArray(
            compiled_descriptor.data(),
            (int)compiled_descriptor.size()
        );
        data.name = data.descriptor.name();
        descriptors[data.name] = data;
        std::println("Found {} in binary file", data.name);
    }
    std::println("");

    load_order.reserve(compiled_descriptors.size());
    descriptor_data.reserve(compiled_descriptors.size());

    while (!descriptors.empty())
        buildProtobufDescriptor(descriptors, descriptors.cbegin()->first);
}

void ProtobufResolver::buildProtobufDescriptor(
    std::unordered_map<std::string, protobuf_data>& descriptors,
    const std::string& name,
    size_t indent
)
{
    auto& data = descriptors[name];
    auto& proto = data.descriptor;

    if (indent) std::print("{:{}}", "", indent);
    std::println("Loading {} ({} dependencies)", name, proto.dependency_size());

    for (int i = 0; i < proto.dependency_size(); ++i) {
        const std::string& dependency = proto.dependency(i);
        if (pool.FindFileByName(dependency))
            continue;
        buildProtobufDescriptor(descriptors, dependency, indent + 3);
    }

    data.definition = pool.BuildFile(proto)->DebugString();
    data.compiled_name = name.substr(0, name.find_last_of('.')) + ".pb";
    load_order.push_back(name);
    descriptor_data[name] = descriptors[name]; // todo: better method?
    descriptors.erase(name);
}

const std::vector<std::string> ProtobufResolver::getLoadOrder() {
    return load_order;
}

std::string ProtobufResolver::getLoadOrderAsJson() {

    std::string ret = "[";
    for (int i = 0; i < load_order.size(); ++i) {
        if (i)
            ret += ",";
        ret += std::format("\n    \"{}\"", descriptor_data[load_order[i]].compiled_name);
    }
    ret += "\n]";
    return ret;
}

void ProtobufResolver::dumpFile(std::filesystem::path output_directory, std::string name) {
    const std::filesystem::path dir = std::filesystem::path(name).parent_path();
    const std::filesystem::path output_file = output_directory / "proto" / name;
    const auto& proto = descriptor_data[name];

    std::println("Extracting {}", name);

    // Check if file already exists
    std::ifstream existing(output_file, std::ios::ate);
    if (existing.fail()) {
        // Does not already exist
        std::println(">>> New proto file: {}", name);
    }
    else {
        // Already exists. Check if it is equal to what we are going to output.
        // We cannot short-circuit this by comparing file sizes, due to different newline formats - \n vs \r\n
        existing.seekg(0, std::ios::beg);
        bool equal = std::equal(
            std::istreambuf_iterator<char>(existing.rdbuf()),
            std::istreambuf_iterator<char>(),
            proto.definition.begin());
        existing.close();

        if (!equal) {
            // If not equal, back up old file for reference
            std::println(">>> {} has changed", name);
            std::filesystem::rename(output_file, output_file.string() + ".old");
        }
    }

    std::filesystem::create_directories(output_directory / "proto" / dir);
    std::ofstream out(output_file);
    out << proto.definition;

    std::filesystem::create_directories(output_directory / "pb" / dir);
    out = std::ofstream(output_directory / "pb" / proto.compiled_name, std::ios::binary);
    std::copy(proto.compiled.cbegin(), proto.compiled.cend(), std::ostream_iterator<uint8_t>(out));
}

void ProtobufResolver::dumpFiles(std::filesystem::path output_directory) {
    for (const auto& file : load_order) {
        dumpFile(output_directory, file);
    }
}
