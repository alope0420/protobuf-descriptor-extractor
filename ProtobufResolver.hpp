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

#include <print>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>

#pragma warning (push)
#pragma warning (disable: 4251)
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#pragma warning (pop)


class ProtobufResolver {

public:
    struct protobuf_data {
        std::string name;
        std::string compiled_name;
        std::string definition;
        google::protobuf::FileDescriptorProto descriptor;
        std::vector<uint8_t> compiled;
    };

    bool backup_replaced_dump_files = true;

    ProtobufResolver(const std::vector<std::vector<uint8_t>>& compiled_descriptors);
    const std::vector<std::string>& getLoadOrder();
    std::string getLoadOrderAsJson();
    void dumpFile(const std::filesystem::path& output_directory, const std::string& name);
    void dumpFiles(const std::filesystem::path& output_directory);

private:
    void buildProtobufDescriptor(
        std::unordered_set<std::string>& unloaded_descriptors,
        const std::string& name,
        size_t indent = 0
    );

    void compareToExistingProto(const std::filesystem::path& existing_file, const ProtobufResolver::protobuf_data& descriptor);
    void createDirectoriesFor(const std::filesystem::path& base_directory, const std::string& file_name);
    void extractProto(const std::filesystem::path& output_directory, const ProtobufResolver::protobuf_data& descriptor);
    void extractCompiledProto(const std::filesystem::path& output_directory, const ProtobufResolver::protobuf_data& descriptor);

    google::protobuf::DescriptorPool pool;
    std::vector<std::string> load_order;
    std::unordered_map<std::string, protobuf_data> descriptors;
};