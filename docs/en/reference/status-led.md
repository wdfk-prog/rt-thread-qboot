# QBoot Status LED

## 1. Scope

When the status LED feature is enabled, QBoot can use blink rate to indicate runtime state. This is useful on products that do not always have a reliable log output but still need quick field diagnosis.

## 2. State table

| Blink rate | Meaning |
|---|---|
| 2 | normal running |
| 4 | factory key is pressed and press duration is being checked |
| 10 | releasing firmware package |
| 1 | shell command mode entered |

## 3. Recommendations

- keep the status LED if the device does not have a reliable log interface
- review power impact on low-power products
- decide whether to keep the feature in production based on hardware constraints
