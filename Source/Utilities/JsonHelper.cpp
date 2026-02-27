/**
EnigmaFix Copyright (c) 2026 Bryce Q.

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
**/

#include "JsonHelper.h"

namespace JsonUtils {
    // Function to load a JSON file and return the parsed object
    nlohmann::json LoadJson(const std::string& path)
    {
        // Open the file in binary mode to avoid issues with encoding
        std::ifstream file(path, std::ios::in | std::ios::binary);

        // Check if the file was successfully opened
        if (!file.is_open()) {
            std::cerr << "Failed to open JSON file: " << path << std::endl;
            return {};  // Return an empty JSON object if file cannot be opened
        }

        // Read the entire file content into a string
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // Declare a JSON object to hold the parsed data
        nlohmann::json loadedJson;

        try {
            // Attempt to parse the file content into a JSON object
            loadedJson = nlohmann::json::parse(content);
        } catch (const nlohmann::json::parse_error& e) {
            // If parsing fails, output the error and return an empty JSON object
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
            return {};  // Return an empty JSON object if parsing fails
        }

        // Return the successfully parsed JSON object
        return loadedJson;
    }
} // JsonUtils