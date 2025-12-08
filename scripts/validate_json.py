#!/usr/bin/env python3
"""
JSON validation for Cat Annihilation game data
Validates quest structure, NPC data, dialog trees, and item database
"""

import os
import sys
import json
from pathlib import Path
from typing import Dict, List, Any, Set

PROJECT_ROOT = Path(__file__).parent.parent.resolve()
ASSETS_DIR = PROJECT_ROOT / "assets"

class ValidationError:
    def __init__(self, file: str, error: str, severity: str = "error"):
        self.file = file
        self.error = error
        self.severity = severity

    def __str__(self):
        symbol = "✗" if self.severity == "error" else "⚠"
        return f"{symbol} {self.file}: {self.error}"

def find_json_files() -> List[Path]:
    """Find all JSON files in assets directory"""
    if not ASSETS_DIR.exists():
        return []
    return list(ASSETS_DIR.rglob("*.json"))

def validate_json_syntax(file_path: Path) -> List[ValidationError]:
    """Validate JSON syntax"""
    errors = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            json.load(f)
    except json.JSONDecodeError as e:
        errors.append(ValidationError(
            str(file_path.relative_to(PROJECT_ROOT)),
            f"JSON syntax error: {e.msg} at line {e.lineno}, column {e.colno}"
        ))
    except Exception as e:
        errors.append(ValidationError(
            str(file_path.relative_to(PROJECT_ROOT)),
            f"Error reading file: {str(e)}"
        ))
    return errors

def validate_quest_data(file_path: Path, data: Dict) -> List[ValidationError]:
    """Validate quest structure"""
    errors = []
    rel_path = str(file_path.relative_to(PROJECT_ROOT))

    # Check if it's a quest file
    if "quests" not in file_path.parts:
        return errors

    # Validate quests array
    if not isinstance(data, dict):
        errors.append(ValidationError(rel_path, "Quest file must be a JSON object"))
        return errors

    quests = data.get("quests", [])
    if not isinstance(quests, list):
        errors.append(ValidationError(rel_path, "'quests' must be an array"))
        return errors

    required_fields = ["id", "title", "description"]

    for i, quest in enumerate(quests):
        if not isinstance(quest, dict):
            errors.append(ValidationError(rel_path, f"Quest {i} is not an object"))
            continue

        # Check required fields
        for field in required_fields:
            if field not in quest:
                errors.append(ValidationError(
                    rel_path,
                    f"Quest {quest.get('id', i)} missing required field: {field}"
                ))

        # Validate objectives
        if "objectives" in quest:
            objectives = quest["objectives"]
            if not isinstance(objectives, list):
                errors.append(ValidationError(
                    rel_path,
                    f"Quest {quest.get('id')} objectives must be an array"
                ))
            else:
                for j, obj in enumerate(objectives):
                    if not isinstance(obj, dict):
                        errors.append(ValidationError(
                            rel_path,
                            f"Quest {quest.get('id')} objective {j} is not an object"
                        ))
                    elif "description" not in obj:
                        errors.append(ValidationError(
                            rel_path,
                            f"Quest {quest.get('id')} objective {j} missing description",
                            "warning"
                        ))

    return errors

def validate_npc_data(file_path: Path, data: Dict) -> List[ValidationError]:
    """Validate NPC data"""
    errors = []
    rel_path = str(file_path.relative_to(PROJECT_ROOT))

    if "npcs" not in file_path.parts:
        return errors

    npcs = data.get("npcs", [])
    if not isinstance(npcs, list):
        errors.append(ValidationError(rel_path, "'npcs' must be an array"))
        return errors

    required_fields = ["id", "name", "type"]

    for i, npc in enumerate(npcs):
        if not isinstance(npc, dict):
            errors.append(ValidationError(rel_path, f"NPC {i} is not an object"))
            continue

        for field in required_fields:
            if field not in npc:
                errors.append(ValidationError(
                    rel_path,
                    f"NPC {npc.get('id', i)} missing required field: {field}"
                ))

        # Validate position if present (accepts both array [x,y,z] and object {x,y,z})
        if "position" in npc:
            pos = npc["position"]
            if isinstance(pos, list):
                # Array format [x, y, z] is valid
                if len(pos) != 3:
                    errors.append(ValidationError(
                        rel_path,
                        f"NPC {npc.get('id')} position array must have 3 elements",
                        "warning"
                    ))
            elif isinstance(pos, dict):
                # Object format {x, y, z} is also valid
                for coord in ["x", "y", "z"]:
                    if coord not in pos:
                        errors.append(ValidationError(
                            rel_path,
                            f"NPC {npc.get('id')} position missing {coord} coordinate",
                            "warning"
                        ))
            else:
                errors.append(ValidationError(
                    rel_path,
                    f"NPC {npc.get('id')} position must be an array or object"
                ))

    return errors

def validate_dialog_data(file_path: Path, data: Dict) -> List[ValidationError]:
    """Validate dialog tree structure"""
    errors = []
    rel_path = str(file_path.relative_to(PROJECT_ROOT))

    if "dialogs" not in file_path.parts and "dialog" not in file_path.parts:
        return errors

    # Check dialog structure
    if "nodes" in data:
        nodes = data["nodes"]
        if not isinstance(nodes, list):
            errors.append(ValidationError(rel_path, "'nodes' must be an array"))
            return errors

        node_ids = set()
        referenced_ids = set()

        for i, node in enumerate(nodes):
            if not isinstance(node, dict):
                errors.append(ValidationError(rel_path, f"Dialog node {i} is not an object"))
                continue

            # Track node IDs
            if "id" in node:
                node_ids.add(node["id"])
            else:
                errors.append(ValidationError(
                    rel_path,
                    f"Dialog node {i} missing 'id' field"
                ))

            # Check for text
            if "text" not in node and "speaker" in node:
                errors.append(ValidationError(
                    rel_path,
                    f"Dialog node {node.get('id', i)} missing 'text' field",
                    "warning"
                ))

            # Track referenced node IDs in choices
            if "choices" in node:
                choices = node["choices"]
                if isinstance(choices, list):
                    for choice in choices:
                        if isinstance(choice, dict) and "next" in choice:
                            referenced_ids.add(choice["next"])

        # Check for unreachable nodes (referenced but don't exist)
        unreachable = referenced_ids - node_ids
        if unreachable:
            errors.append(ValidationError(
                rel_path,
                f"Dialog references non-existent nodes: {', '.join(map(str, unreachable))}",
                "warning"
            ))

    return errors

def validate_items_data(file_path: Path, data: Dict) -> List[ValidationError]:
    """Validate item database"""
    errors = []
    rel_path = str(file_path.relative_to(PROJECT_ROOT))

    if "items" not in file_path.name:
        return errors

    items = data.get("items", [])
    if not isinstance(items, list):
        errors.append(ValidationError(rel_path, "'items' must be an array"))
        return errors

    valid_categories = {"weapon", "armor", "consumable", "quest", "material", "misc"}

    for i, item in enumerate(items):
        if not isinstance(item, dict):
            errors.append(ValidationError(rel_path, f"Item {i} is not an object"))
            continue

        # Check required fields - accept both 'id' and 'itemId'
        item_id = item.get("id") or item.get("itemId")
        if not item_id:
            errors.append(ValidationError(rel_path, f"Item {i} missing 'id' or 'itemId' field"))

        if "name" not in item:
            errors.append(ValidationError(rel_path, f"Item {item_id or i} missing 'name' field"))

        # Validate category (case-insensitive)
        if "category" in item:
            category_lower = item["category"].lower() if isinstance(item["category"], str) else ""
            if category_lower not in valid_categories:
                errors.append(ValidationError(
                    rel_path,
                    f"Item {item_id} has invalid category: {item['category']}",
                    "warning"
                ))

        # Validate price is positive
        if "price" in item:
            if not isinstance(item["price"], (int, float)) or item["price"] < 0:
                errors.append(ValidationError(
                    rel_path,
                    f"Item {item.get('id')} has invalid price: {item['price']}",
                    "warning"
                ))

    return errors

def main():
    print("=" * 80)
    print("CAT ANNIHILATION - JSON VALIDATION")
    print("=" * 80)
    print()

    if not ASSETS_DIR.exists():
        print(f"ERROR: Assets directory not found: {ASSETS_DIR}")
        return 1

    files = find_json_files()
    print(f"Found {len(files)} JSON files to validate")
    print()

    all_errors = []
    files_with_errors = set()

    # Validate each file
    for file_path in files:
        rel_path = file_path.relative_to(PROJECT_ROOT)

        # First check JSON syntax
        syntax_errors = validate_json_syntax(file_path)
        if syntax_errors:
            all_errors.extend(syntax_errors)
            files_with_errors.add(str(rel_path))
            continue  # Skip schema validation if syntax is broken

        # Load data for schema validation
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                data = json.load(f)

            # Validate based on file type
            errors = []
            errors.extend(validate_quest_data(file_path, data))
            errors.extend(validate_npc_data(file_path, data))
            errors.extend(validate_dialog_data(file_path, data))
            errors.extend(validate_items_data(file_path, data))

            if errors:
                all_errors.extend(errors)
                files_with_errors.add(str(rel_path))

        except Exception as e:
            all_errors.append(ValidationError(str(rel_path), f"Unexpected error: {str(e)}"))
            files_with_errors.add(str(rel_path))

    # Print results
    print("=" * 80)
    print("RESULTS")
    print("=" * 80)
    print()

    if all_errors:
        # Group by severity
        errors = [e for e in all_errors if e.severity == "error"]
        warnings = [e for e in all_errors if e.severity == "warning"]

        if errors:
            print(f"ERRORS: {len(errors)}")
            for error in errors[:20]:
                print(f"  {error}")
            if len(errors) > 20:
                print(f"  ... and {len(errors) - 20} more errors")
            print()

        if warnings:
            print(f"WARNINGS: {len(warnings)}")
            for warning in warnings[:20]:
                print(f"  {warning}")
            if len(warnings) > 20:
                print(f"  ... and {len(warnings) - 20} more warnings")
            print()
    else:
        print("✓ All JSON files are valid!")
        print()

    # Summary
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total files:        {len(files)}")
    print(f"Files with issues:  {len(files_with_errors)}")
    print(f"Total errors:       {len([e for e in all_errors if e.severity == 'error'])}")
    print(f"Total warnings:     {len([e for e in all_errors if e.severity == 'warning'])}")
    print()

    if all_errors and any(e.severity == "error" for e in all_errors):
        print("FAILED: JSON validation found errors")
        return 1
    elif all_errors:
        print("PASSED: JSON validation passed with warnings")
        return 0
    else:
        print("SUCCESS: All JSON files validated successfully!")
        return 0

if __name__ == "__main__":
    sys.exit(main())
