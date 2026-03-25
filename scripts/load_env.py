import os
from pathlib import Path

Import("env")


def _parse_dotenv(path: Path):
    data = {}
    if not path.exists():
        return data
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if (value.startswith('"') and value.endswith('"')) or (value.startswith("'") and value.endswith("'")):
            value = value[1:-1]
        data[key] = value
    return data


def _escape_cpp_string(v: str) -> str:
    return v.replace("\\", "\\\\").replace('"', '\\"')


project_dir = Path(env.subst("$PROJECT_DIR"))
dotenv = _parse_dotenv(project_dir / ".env")

token = os.environ.get("PLATFORMIO_GITHUB_TOKEN", "")
if not token:
    token = dotenv.get("PLATFORMIO_GITHUB_TOKEN", "")
if not token:
    token = dotenv.get("GITHUB_TOKEN", "")

if token:
    env.Append(CPPDEFINES=[("GITHUB_TOKEN", '\"{}\"'.format(_escape_cpp_string(token)))])
else:
    env.Append(CPPDEFINES=[("GITHUB_TOKEN", '\"\"')])
