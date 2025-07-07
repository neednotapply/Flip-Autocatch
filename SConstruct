Import("env")

# Add required flipper modules
env.modules.add(
    "lib/notification",
    "lib/dialogs",
)

# FAP descriptor
env.Append(
    FAP={
        "name": "Autocatch",
        "appid": "autocatch",
        "entry_point": "autocatch_app",
        "manifest": "application.fam",
    }
)

# Source files
env.SDKSource(
    files=[
        "autocatch.c",
        "pgp.c",
    ]
)
