import os
import shutil

try:
    Import("env")  # type: ignore  # PlatformIO SCons function, not available in IDE
except:
    # For linting/IDE purposes when not running in PlatformIO
    env = None  # type: ignore

def remove_lvgl_asm_dirs():
    """Remove ARM-specific assembly files from LVGL that cause build errors on ESP32-P4"""

    print("=" * 60)
    print("Running remove_lvgl_asm.py script")
    print("=" * 60)

    # Get project directory
    project_dir = env.get("PROJECT_DIR")

    # Possible LVGL paths
    lvgl_paths = [
        os.path.join(project_dir, ".pio", "libdeps", "esp32-p4", "lvgl"),
        os.path.join(project_dir, "lib", "lvgl"),
        os.path.join(project_dir, ".pio", "libdeps", "*", "lvgl"),
    ]

    lvgl_found = False

    for lvgl_pattern in lvgl_paths:
        # Handle wildcards
        if '*' in lvgl_pattern:
            import glob
            matching_paths = glob.glob(lvgl_pattern)
            for lvgl_path in matching_paths:
                if os.path.exists(lvgl_path):
                    lvgl_found = True
                    process_lvgl_path(lvgl_path)
        else:
            if os.path.exists(lvgl_pattern):
                lvgl_found = True
                process_lvgl_path(lvgl_pattern)

    if not lvgl_found:
        print("WARNING: LVGL library path not found!")
        print("Searched paths:", lvgl_paths)

    print("=" * 60)
    print("Script completed")
    print("=" * 60)

def process_lvgl_path(lvgl_path):
    """Process a single LVGL path and remove ARM assembly files"""
    print(f"Found LVGL at: {lvgl_path}")

    # Paths to remove
    asm_dirs = [
        os.path.join(lvgl_path, 'src', 'draw', 'sw', 'blend', 'helium'),
        os.path.join(lvgl_path, 'src', 'draw', 'sw', 'blend', 'neon'),
    ]

    for asm_dir in asm_dirs:
        if os.path.exists(asm_dir):
            print(f"  Removing ARM ASM directory: {asm_dir}")
            shutil.rmtree(asm_dir)
        else:
            print(f"  ARM ASM directory not found (already removed?): {asm_dir}")

    # Also remove any .S or .s files in blend directory
    blend_dir = os.path.join(lvgl_path, 'src', 'draw', 'sw', 'blend')
    if os.path.exists(blend_dir):
        for root, dirs, files in os.walk(blend_dir):
            for file in files:
                if file.endswith(('.S', '.s')):
                    asm_file = os.path.join(root, file)
                    print(f"  Removing ASM file: {asm_file}")
                    os.remove(asm_file)

# Execute immediately when script loads (pre: scripts run before dependencies are resolved)
remove_lvgl_asm_dirs()
