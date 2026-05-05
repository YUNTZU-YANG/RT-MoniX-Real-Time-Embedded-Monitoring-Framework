# RT-MoniX — Real-Time Embedded Monitoring Framework

ESP32 + FreeRTOS demo (Wokwi simulator).

Overview: synchronized readings from an LDR (ADC1 CH0, `GPIO36`) and a simulated DHT22 (`GPIO4`) every 5 seconds. Uses task notifications for synchronization and a queue (`xCombinedDataQueue`) for combined output.

Quick facts:

- DHT GPIO: `GPIO4`
- LDR ADC: `ADC1 Channel 0` (`GPIO36` / VP)
- Sampling interval: `5 seconds`

Limitations:

- DHT22 microsecond timing is not supported in Wokwi; readings are simulated.
- ADC values are approximate in the simulator and may differ on real hardware.

Run:

- Upload to a Wokwi ESP32 FreeRTOS project and open the Serial Monitor.

For higher fidelity testing, run on a real ESP32 or replace the mock DHT with an I2C/SPI sensor (e.g., SHT31/BME280).


---
TODO
---
目前專案的待辦項目（階段式）：

1. Read Wokwi documentation
	- 檢視 Wokwi 的 ESP32 範例、GPIO 時序與自訂元件文件，確認是否能支援 DHT 或使用 RMT。
2. Design DHT driver approach
	- 決定驅動實作方式（ESP-IDF RMT / bit-bang / 使用 Wokwi JS 元件），並撰寫簡要設計說明與 API。
3. Implement `dht.h` and `dht.c`
	- 建立 `dht.h`/`dht.c`，實作初始化與讀取函式，考量 FreeRTOS 相容性與非阻塞行為。
4. Test in Wokwi and on hardware
	- 在 Wokwi 嘗試執行；若時序失敗則改用 JS 模擬或在實機驗證。
5. Integrate and document
	- 整合驅動到專案、更新 `README.md` 並加入範例輸出或測試紀錄。

（任務狀態請參考專案的 TODO 清單）



