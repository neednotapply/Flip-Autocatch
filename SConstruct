Import("env")

# Add required flipper modules
env.modules.add(
    "lib/notification",
    "lib/dialogs",
)

# Build the Autocatch FAP
env.add_application(
    target="autocatch",
    source_files=["autocatch.c"],
    app_manifest="application.fam",
    assets_dir="images",
)
