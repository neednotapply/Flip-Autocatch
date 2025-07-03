Import("env")

# Add required flipper modules
env.modules.add(
    "lib/notification",
    "lib/dialogs",
)

# Define your FAP build
env.Append(
    FAP={
        "name": "Autocatch",
        "appid": "autocatch",
        "entry_point": "autocatch_app",  # <- matches your actual function name
        "manifest": "application.fam",
    }
)

# Source files
env.SDKSource(
    files=[
        "autocatch.c"
    ]
)
