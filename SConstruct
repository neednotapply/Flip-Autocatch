Import("env")

# Optional Flipper libs (can be removed if not used in code)
env.modules.add(
    "lib/notification",
    "lib/dialogs",
)

env.Append(
    FAP={
        "name": "Autocatch",
        "appid": "autocatch",         # Unique ID (no spaces)
        "entry_point": "autocatch",   # Must match the function name in autocatch.c
        "manifest": "application.fam",
    }
)

env.SDKSource(
    files=[
        "autocatch.c"
    ]
)
