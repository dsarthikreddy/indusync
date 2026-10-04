SARTHIK ROOM v4 - SMART ROOM WEBSITE (built on CLASSYNC 4.0)
=============================================================
ONE TIME
  1. Install.bat                      (needs internet, 5-15 min; last lines: "AI models ready", "Face recognition ready")
  2. Turn On Autostart.bat            (Desktop icon "SARTHIK Room" + starts by itself when you sign in to Windows)
  3. Double-click the "SARTHIK Room" Desktop icon -> create the admin account
  4. Menu > Settings > Door board link: ON, Over Wi-Fi, address from the board (type  wifi  in Serial Monitor) > Save
  5. Make Public Link.bat (optional, needs Tailscale) -> your https://....ts.net link for any phone, anywhere

EVERY DAY
  Nothing to start. Open the Desktop icon or your saved link. Live > Start turns the camera on.

PAGES
  Overview, Live, Control (open door, new day, add card, phones, alerts, phone message, integrations, backups),
  Messages (family chat + private chat), Attendance, People, Faces, Security, History, RFID, Door board,
  Phone key, Wi-Fi radar, Health, Storage, Logs.  Light/dark: the moon/sun button at the top.

FILES
  SARTHIK Room.vbs        opens the website (starts it silently if needed)
  Stop SARTHIK Room.bat   stops the background website
  Start CCTV.bat          runs it in a visible window (use this if something looks wrong: you see the messages)
  Backup to USB.bat       copies backups to a USB drive (unplug the drive afterwards)
  data\server.log         messages of the background website      data\security.log   logins, refusals, admin actions

PROBLEMS
  Icon does nothing        -> run Start CCTV.bat once to see the message
  Door commands fail       -> Control > Integrations > Door key must equal DOOR_API_KEY in the board's secrets.h
  Phone gets nothing       -> install the "ntfy" app, subscribe to the channel shown in Control > Integrations
  Forgot password          -> Stop SARTHIK Room.bat, delete data\users.json and data\sessions.json, open the icon again
  Laptop off               -> the website is off too (24x7 without the laptop = Stage 3 Firebase); the door still works
