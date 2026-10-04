# Test harnesses (used during development, run on a Linux PC, not on the board)
- firmware/: host simulation of the cloud/sensor code. gen82.py extracts the real firmware block into v82.h,
  t82.cpp fakes Firebase/ThingSpeak/Blynk/Telegram/Sheet. Build: `g++ -std=gnu++17 -fsanitize=address,undefined t82.cpp -o t82`,
  then `./t82 <mode>` for modes flow outage badlogin forbid cmd telegram blynk sensors mirror instant blynkmiss blynknovalue, then `python3 check82.py`.
  Paths inside gen82.py point to the original sandbox (/home/claude/...): change them to firmware/door_board_v8_2/... first.
- web/cloud_ui.js: jsdom test of the Firebase website (node + jsdom). web/gs_test.js: Apps Script logic with mocked Google services.
  web/rep_test.py, web/ts_test.py: setup-wizard tests. All use FAKE keys.
Known limit: the board's real memory/TLS behaviour cannot be simulated — always confirm on hardware.
