
Import("env")

env.modules.add(
    "lib/notification",
    "lib/dialogs",
    "lib/ble",          # ← exposes ble_glue headers for external apps
)

env.Append(
    FAP={
        "name": "Autocatch",
        "appid": "autocatch",
        "entry_point": "autocatch_app",
        "manifest": "application.fam",
    }
)

env.SDKSource(files=["autocatch.c", "pgp.c"])
