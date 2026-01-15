#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Utility to load GLSL shader files
inline std::string loadShaderFile(const std::string &filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file: " << filepath << "\n";
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Return empty string if file is empty or only whitespace
    if (content.empty() || content.find_first_not_of(" \t\n\r") == std::string::npos)
    {
        std::cerr << "Warning: Shader file " << filepath << " is empty" << "\n";
        return "";
    }

    return content;
}

// Helper to get shader file path relative to executable
inline std::string getShaderPath(const std::string &filename)
{
    // Try multiple possible paths
    std::vector<std::string> paths = {
        "shaders/" + filename,
        "src/materials/earth/shaders/" + filename,
        "../src/materials/earth/shaders/" + filename,
        "../../src/materials/earth/shaders/" + filename,
    };

    for (const auto &path : paths)
    {
        std::ifstream test(path);
        if (test.good())
        {
            test.close();
            return path;
        }
    }

    // Return first path as default (will fail with error message)
    return paths[0];
}
