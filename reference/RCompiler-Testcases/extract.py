#!/usr/bin/python3
import base64
import json
import os

file_lists_sema_1 = []
file_lists_sema_2 = []
file_lists_IR_1 = []
cnt = 0

with open("testcases.sql", "w") as f:
    f.write(
        "USE compiler;\n"
    )

with open("semantic-1/global.json", "r") as f:
    file_lists_sema_1 = json.load(f)

with open("semantic-2/global.json", "r") as f:
    file_lists_sema_2 = json.load(f)

with open("IR-1/global.json", "r") as f:
    file_lists_IR_1 = json.load(f)

phase_id = 1

for case in file_lists_sema_1:
    cnt += 1
    case_name = case.get("name", "")

    # Extract case name from path
    source = case.get("source", "")[0]
    input = case.get("input", "")[0]
    output = case.get("output", "")[0]

    # 0 = should compile successfully
    # 1 = should fail compilation
    compile_result = -case.get("compileexitcode", 0)

    # Read the test case file
    content = None
    try:
        with open("semantic-1/" + source, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading {case}: {e}")
        continue
    
    # Encode code as base64
    base64_code = base64.b64encode(content.encode()).decode()
    
    # Generate SQL INSERT statement
    try: 
        with open("testcases.sql", "a") as f:
            f.write(
                f'INSERT INTO TestCases (test_case_id, test_case_disp_name, problem_phase, source_code_base64, input_txt_base64, answer_txt_base64, verdict)'
                f'VALUES ({cnt}, "{case_name}", {phase_id}, "{base64_code}", "", "", {compile_result});\n'
            )
    except Exception as e:
        print(f"Error writing SQL for {case}: {e}")


phase_id = 2


for case in file_lists_sema_2:
    cnt += 1
    case_name = case.get("name", "")

    # Extract case name from path
    source = case.get("source", "")[0]
    input = case.get("input", "")[0]
    output = case.get("output", "")[0]

    # 0 = should compile successfully
    # 1 = should fail compilation
    compile_result = -case.get("compileexitcode", 0)

    # Read the test case file
    content = None
    try:
        with open("semantic-2/" + source, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading {case}: {e}")
        continue
    
    # Encode code as base64
    base64_code = base64.b64encode(content.encode()).decode()
    
    # Generate SQL INSERT statement
    try: 
        with open("testcases.sql", "a") as f:
            f.write(
                f'INSERT INTO TestCases (test_case_id, test_case_disp_name, problem_phase, source_code_base64, input_txt_base64, answer_txt_base64, verdict)'
                f'VALUES ({cnt}, "{case_name}", {phase_id}, "{base64_code}", "", "", {compile_result});\n'
            )
    except Exception as e:
        print(f"Error writing SQL for {case}: {e}")


phase_id = 3

for case in file_lists_IR_1:
    cnt += 1
    case_name = case.get("name", "")

    # Extract case name from path
    source = case.get("source", "")[0]
    input = case.get("input", "")[0]
    output = case.get("output", "")[0]

    # Read the test case file
    content = None
    try:
        with open("IR-1/" + source, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading {case}: {e}")
        continue
    
    base64_code = base64.b64encode(content.encode()).decode()

    try:
        with open("IR-1/" + input, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading {case}: {e}")
        continue

    base64_input = base64.b64encode(content.encode()).decode()

    try:
        with open("IR-1/" + output, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading {case}: {e}")
        continue

    base64_output = base64.b64encode(content.encode()).decode()
    
    # Generate SQL INSERT statement
    try: 
        with open("testcases.sql", "a") as f:
            f.write(
                f'INSERT INTO TestCases (test_case_id, test_case_disp_name, problem_phase, source_code_base64, input_txt_base64, answer_txt_base64, verdict)'
                f'VALUES ({cnt}, "{case_name}", {phase_id}, "{base64_code}", "{base64_input}", "{base64_output}", 0);\n'
            )
    except Exception as e:
        print(f"Error writing SQL for {case}: {e}")
