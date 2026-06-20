import os
import re
import sys

def amalgamate(entry_point, include_dir):
    visited = set()
    system_includes = set()
    output_lines = []

    include_pattern = re.compile(r'^\s*#include\s+["<](.*?)[">]')

    def is_system_include(line):
        return line.strip().startswith('#include <')

    def process_file(filepath):
        norm_path = os.path.normpath(filepath)
        if norm_path in visited:
            return
        visited.add(norm_path)

        if not os.path.exists(filepath):
            print(f"Warning: could not find {filepath}", file=sys.stderr)
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        for line in lines:
            line_stripped = line.strip()
            if line_stripped == '#pragma once':
                continue
            
            match = include_pattern.search(line)
            if match:
                inc_path = match.group(1)
                is_system = is_system_include(line)
                
                if is_system:
                    system_includes.add(line_stripped)
                else:
                    # check if the file exists in include_dir
                    target_file = os.path.join(include_dir, inc_path)
                    if os.path.exists(target_file):
                        process_file(target_file)
                    else:
                        output_lines.append(line)
            else:
                output_lines.append(line)

    process_file(entry_point)

    final_output = []
    final_output.append("#pragma once\n")
    final_output.append("// Amalgamated header for jaggedcpp\n\n")

    for sys_inc in sorted(list(system_includes)):
        final_output.append(sys_inc + "\n")
    
    final_output.append("\n")

    previous_blank = False
    for line in output_lines:
        is_blank = (line.strip() == "")
        if previous_blank and is_blank:
            continue
        final_output.append(line)
        previous_blank = is_blank

    return "".join(final_output)

if __name__ == "__main__":
    include_dir = "include"
    
    # Process both awkward/awkward.hpp and jagged/jagged.hpp if needed, 
    # but the user asked for `awkward.h`, so let's start with awkward/awkward.hpp
    entry_point = "include/awkward/awkward.hpp"
    # To support jagged compatibility, let's parse jagged/jagged.hpp too and append
    
    output1 = amalgamate("include/awkward/awkward.hpp", include_dir)
    
    # Then write to awkward.h
    with open("awkward.h", "w", encoding="utf-8") as f:
        f.write(output1)
        
    print("Created awkward.h successfully.")
