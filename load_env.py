import os

Import("env")

project_dir = env.subst("$PROJECT_DIR")
env_file = os.path.join(project_dir, ".env")
secrets_file = os.path.join(project_dir, "src", "secrets.h")

if not os.path.exists(env_file):
    print("WARNING: .env file not found! Copy .env.example to .env and fill in values.")
else:
    # Generate secrets.h from .env
    values = {}
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()

    with open(secrets_file, "w") as f:
        f.write("// AUTO-GENERATED from .env — do not edit or commit\n")
        f.write("#pragma once\n\n")
        for key, value in values.items():
            f.write('#define %s "%s"\n' % (key, value))

    print("Generated secrets.h from .env")
