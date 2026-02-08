#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <limits>

namespace fs = std::filesystem;

void print_menu() {
    std::cout << "Select function:\n";
    std::cout << "1. Compile (lua -> luac)\n";
    std::cout << "2. Disassemble (luac -> asm)\n";
    std::cout << "3. Assemble (asm -> luac)\n";
    std::cout << "4. Decompile (luac -> lua)\n";
    std::cout << "Enter choice (1-4): ";
}

std::string get_input(const std::string& prompt) {
    std::cout << prompt;
    std::string input;
    std::getline(std::cin, input);
    return input;
}

int main(int argc, char* argv[]) {
    int choice = 0;
    std::string input_path;
    std::string output_path;

    if (argc > 1) {
        std::cerr << "This wrapper is designed for interactive use. Run without arguments." << std::endl;
        return 1;
    }

    print_menu();
    if (!(std::cin >> choice)) {
        std::cerr << "Invalid input." << std::endl;
        return 1;
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // clear buffer

    if (choice < 1 || choice > 4) {
        std::cerr << "Invalid choice." << std::endl;
        return 1;
    }

    input_path = get_input("Enter input file path: ");
    if (!fs::exists(input_path)) {
        std::cerr << "Input file does not exist: " << input_path << std::endl;
        return 1;
    }

    output_path = get_input("Enter output file path: ");

    std::string cmd;
    std::string base_dir = ".";
    if (argc > 0 && argv[0]) {
        fs::path p(argv[0]);
        if (p.has_parent_path()) {
            base_dir = p.parent_path().string();
        }
    }

    // Ensure we have the slash if needed
    // But if base_dir is "ALCC", we want "ALCC/alcc-c".
    // If base_dir is ".", we want "./alcc-c".
    // fs::path append operator handles separators.

    fs::path tool_path;

    switch (choice) {
        case 1: // Compile: alcc-c input -o output
            tool_path = fs::path(base_dir) / "alcc-c";
            cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" -o \"" + output_path + "\"";
            break;
        case 2: // Disassemble: alcc-d input > output
            tool_path = fs::path(base_dir) / "alcc-d";
            cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" > \"" + output_path + "\"";
            break;
        case 3: // Assemble: alcc-a input -o output
            tool_path = fs::path(base_dir) / "alcc-a";
            cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" -o \"" + output_path + "\"";
            break;
        case 4: // Decompile: alcc-dec input > output
            tool_path = fs::path(base_dir) / "alcc-dec";
            cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" > \"" + output_path + "\"";
            break;
    }

    std::cout << "Executing: " << cmd << std::endl;
    int ret = std::system(cmd.c_str());

    if (ret != 0) {
        std::cerr << "Error executing command." << std::endl;
        return ret;
    }

    std::cout << "Done." << std::endl;
    return 0;
}
