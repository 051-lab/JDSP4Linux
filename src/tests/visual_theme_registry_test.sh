#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
header="$root/utils/VisualTheme.h"
source="$root/utils/VisualTheme.cpp"
resource="$root/../resources/resources.qrc"

test -f "$header"
test -f "$source"

for theme in classic studio midnight paper highcontrast; do
    grep -Fq "\"$theme\"" "$source"
    test -f "$root/../resources/styles/themes/$theme.qss"
    grep -Fq "styles/themes/$theme.qss" "$resource"
done

grep -Fq 'VisualTheme' "$root/config/AppConfig.h"
grep -Fq 'VisualTheme' "$root/config/AppConfig.cpp"
grep -Fq 'visualThemeSelect' "$root/interface/fragment/SettingsFragment.ui"
grep -Fq 'VisualThemeProvider' "$root/utils/StyleHelper.cpp"
grep -Fq 'Visual theme' "$root/interface/fragment/SettingsFragment.ui"

echo 'visual theme registry contract passed'
