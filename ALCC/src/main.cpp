#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <limits>

namespace fs = std::filesystem;

void print_menu() {
    std::cout << "\nSelect function:\n";
    std::cout << "1. Compile (lua -> luac)\n";
    std::cout << "2. Disassemble (luac -> asm)\n";
    std::cout << "3. Assemble (asm -> luac)\n";
    std::cout << "4. Decompile (luac -> lua)\n";
    std::cout << "5. Switch Template (Current: <TEMPLATE>)\n";
    std::cout << "6. Exit\n";
    std::cout << "Enter choice (1-6): ";
}

std::string get_input(const std::string& prompt) {
    std::cout << prompt;
    std::string input;
    std::getline(std::cin, input);
    return input;
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::cerr << "This wrapper is designed for interactive use. Run without arguments." << std::endl;
        return 1;
    }

    std::string current_template = "default";
    std::string base_dir = ".";
    if (argc > 0 && argv[0]) {
        fs::path p(argv[0]);
        if (p.has_parent_path()) {
            base_dir = p.parent_path().string();
        }
    }

    while (true) {
        // Dynamic menu display
        std::cout << "\nSelect function:\n";
        std::cout << "1. Compile (lua -> luac)\n";
        std::cout << "2. Disassemble (luac -> asm)\n";
        std::cout << "3. Assemble (asm -> luac)\n";
        std::cout << "4. Decompile (luac -> lua)\n";
        std::cout << "5. Switch Template (Current: " << current_template << ")\n";
        std::cout << "6. Exit\n";
        std::cout << "Enter choice (1-6): ";

        int choice = 0;
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cerr << "Invalid input." << std::endl;
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // clear buffer

        if (choice == 6) {
            break;
        }

        if (choice < 1 || choice > 6) {
            std::cerr << "Invalid choice." << std::endl;
            continue;
        }

        fs::path tool_path;
        std::string cmd;

        if (choice == 5) {
            // List templates
            tool_path = fs::path(base_dir) / "alcc-dec";
            cmd = "\"" + tool_path.string() + "\" --list-templates";
            std::system(cmd.c_str());

            std::string new_tpl = get_input("Enter template name: ");
            bool valid = !new_tpl.empty();
            for (char c : new_tpl) {
                if (!isalnum(c) && c != '_' && c != '-') {
                    valid = false;
                    break;
                }
            }

            if (valid) {
                current_template = new_tpl;
                std::cout << "Template switched to: " << current_template << std::endl;
            } else if (!new_tpl.empty()) {
                std::cerr << "Invalid template name. Only alphanumeric characters, hyphens, and underscores are allowed." << std::endl;
            }
            continue;
        }

        std::string input_path = get_input("Enter input file path: ");
        if (!fs::exists(input_path)) {
            std::cerr << "Input file does not exist: " << input_path << std::endl;
            continue;
        }

        std::string output_path = get_input("Enter output file path: ");

#ifndef TOOL_SUFFIX
#define TOOL_SUFFIX ""
#endif

        switch (choice) {
            case 1: // Compile: alcc-c input -o output
                tool_path = fs::path(base_dir) / ("alcc-c" TOOL_SUFFIX);
                cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" -o \"" + output_path + "\"";
                break;
            case 2: // Disassemble: alcc-d input > output
                tool_path = fs::path(base_dir) / ("alcc-d" TOOL_SUFFIX);
                cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" -t " + current_template + " > \"" + output_path + "\"";
                break;
            case 3: // Assemble: alcc-a input -o output
                tool_path = fs::path(base_dir) / ("alcc-a" TOOL_SUFFIX);
                cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" -o \"" + output_path + "\" -t " + current_template;
                break;
            case 4: // Decompile: alcc-dec input > output
                tool_path = fs::path(base_dir) / ("alcc-dec" TOOL_SUFFIX);
                cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" -t " + current_template + " > \"" + output_path + "\"";
                break;
        }

        std::cout << "Executing: " << cmd << std::endl;
        int ret = std::system(cmd.c_str());

        if (ret != 0) {
            std::cerr << "Error executing command." << std::endl;
        } else {
            std::cout << "Done." << std::endl;
        }
    }

    return 0;
}
