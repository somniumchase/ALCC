#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <fstream>
#include <vector>
#include <sstream>

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
        std::cout << "5. Generate CFG (luac -> dot)\n";
        std::cout << "6. View Info (luac -> info)\n";
        std::cout << "7. Switch Template (Current: " << current_template << ")\n";
        std::cout << "8. Exit\n";
        std::cout << "Enter choice (1-8): ";

        int choice = 0;
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cerr << "Invalid input." << std::endl;
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // clear buffer

        if (choice == 8) {
            break;
        }

        if (choice < 1 || choice > 8) {
            std::cerr << "Invalid choice." << std::endl;
            continue;
        }

        fs::path tool_path;
        std::string cmd;

        if (choice == 7) {
            // List templates
            tool_path = fs::path(base_dir) / "alcc-dec";
            cmd = "\"" + tool_path.string() + "\" --list-templates";

            std::vector<std::string> templates;
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buffer[256];
                std::string result = "";
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    result += buffer;
                }
                pclose(pipe);

                std::istringstream stream(result);
                std::string line;
                while (std::getline(stream, line)) {
                    if (line.rfind("Available templates:", 0) == 0) continue;
                    // trim spaces and newlines
                    size_t start = line.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = line.find_last_not_of(" \t\r\n");
                        templates.push_back(line.substr(start, end - start + 1));
                    }
                }
            }

            if (templates.empty()) {
                std::cerr << "No templates found or could not read templates." << std::endl;
                continue;
            }

            std::cout << "\nAvailable templates:\n";
            for (size_t i = 0; i < templates.size(); ++i) {
                std::cout << i + 1 << ". " << templates[i] << "\n";
            }
            std::cout << "Enter choice (1-" << templates.size() << "): ";

            int tpl_choice = 0;
            if (!(std::cin >> tpl_choice)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cerr << "Invalid input." << std::endl;
                continue;
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            if (tpl_choice >= 1 && tpl_choice <= (int)templates.size()) {
                current_template = templates[tpl_choice - 1];
                std::cout << "Template switched to: " << current_template << std::endl;
            } else {
                std::cerr << "Invalid choice." << std::endl;
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
            case 5: // CFG Gen: alcc-cfg input > output
                tool_path = fs::path(base_dir) / ("alcc-cfg" TOOL_SUFFIX);
                cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" > \"" + output_path + "\"";
                break;
            case 6: // Info Gen: alcc-info input > output
                tool_path = fs::path(base_dir) / ("alcc-info" TOOL_SUFFIX);
                cmd = "\"" + tool_path.string() + "\" \"" + input_path + "\" > \"" + output_path + "\"";
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
