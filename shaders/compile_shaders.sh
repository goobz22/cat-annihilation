#!/bin/bash
# Shader Compilation Script for Cat Annihilation
# Compiles all GLSL shaders to SPIR-V bytecode

set -e  # Exit on error

SHADER_DIR="$(dirname "$0")"
OUTPUT_DIR="$SHADER_DIR/compiled"

echo "==================================="
echo "Cat Annihilation Shader Compiler"
echo "==================================="
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Function to compile shader
compile_shader() {
    local file=$1
    local stage=$2
    local output="$OUTPUT_DIR/$(basename $file).spv"

    echo "  Compiling $(basename $file)..."

    if glslc -fshader-stage=$stage "$file" -o "$output" 2>&1; then
        echo "    ✓ Success"
        return 0
    else
        echo "    ✗ Failed"
        return 1
    fi
}

# Compilation statistics
total=0
success=0
failed=0

echo "Compiling Vertex Shaders..."
echo "----------------------------"
for file in "$SHADER_DIR"/geometry/*.vert \
            "$SHADER_DIR"/forward/*.vert \
            "$SHADER_DIR"/sky/*.vert \
            "$SHADER_DIR"/ui/*.vert \
            "$SHADER_DIR"/lighting/*.vert \
            "$SHADER_DIR"/postprocess/*.vert \
            "$SHADER_DIR"/shadows/*.vert; do
    if [ -f "$file" ]; then
        ((total++))
        if compile_shader "$file" vert; then
            ((success++))
        else
            ((failed++))
        fi
    fi
done

echo ""
echo "Compiling Fragment Shaders..."
echo "-----------------------------"
for file in "$SHADER_DIR"/geometry/*.frag \
            "$SHADER_DIR"/forward/*.frag \
            "$SHADER_DIR"/sky/*.frag \
            "$SHADER_DIR"/ui/*.frag \
            "$SHADER_DIR"/lighting/*.frag \
            "$SHADER_DIR"/postprocess/*.frag \
            "$SHADER_DIR"/shadows/*.frag; do
    if [ -f "$file" ]; then
        ((total++))
        if compile_shader "$file" frag; then
            ((success++))
        else
            ((failed++))
        fi
    fi
done

echo ""
echo "Compiling Compute Shaders..."
echo "----------------------------"
for file in "$SHADER_DIR"/compute/*.comp \
            "$SHADER_DIR"/lighting/*.comp; do
    if [ -f "$file" ]; then
        ((total++))
        if compile_shader "$file" comp; then
            ((success++))
        else
            ((failed++))
        fi
    fi
done

echo ""
echo "==================================="
echo "Compilation Summary"
echo "==================================="
echo "Total shaders: $total"
echo "Successful:    $success"
echo "Failed:        $failed"
echo ""

if [ $failed -eq 0 ]; then
    echo "✓ All shaders compiled successfully!"
    echo "Output directory: $OUTPUT_DIR"
    exit 0
else
    echo "✗ Some shaders failed to compile"
    exit 1
fi
