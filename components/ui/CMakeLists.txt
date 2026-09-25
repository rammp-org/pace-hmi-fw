# The SquareLine Studio export, mirrored here by import_ui.ps1. This file is
# kept out of that mirror (import_ui.ps1's $Excluded). lvgl comes from main's
# manifest: a manifest here would make the component manager re-solve the lock.
idf_component_register(SRC_DIRS "." "screens" "components" "fonts" "images"
                       INCLUDE_DIRS "."
                       REQUIRES lvgl)
