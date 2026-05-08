# Light Watcher
### Телеграм бот на ESP32 для моніторингу електромережі

<!-- 
Keywords: ESP32, Telegram Bot, Power Monitoring, Ukraine, Blackout, DIY, Arduino, 
IoT, Home Automation, Battery Backup, Light Monitoring, Електрика, Світло, 
Моніторинг, Блекаут, Україна, ESP32C3, пінгування світла, світлобот, 3д друк.
-->

[![Arduino](https://img.shields.io/badge/Arduino-00979D?style=for-the-badge&logo=Arduino&logoColor=white)](https://www.arduino.cc/)[![ESP32](https://img.shields.io/badge/ESP32-000000?style=for-the-badge&logo=espressif&logoColor=white)](https://www.espressif.com/)[![Telegram](https://img.shields.io/badge/Telegram-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white)](https://telegram.org/)


| Відео-інструкція | Детальний опис та навігація |
| :--- | :--- |
| [![LightWatcher Guide](https://img.youtube.com/vi/MIB5mcXAo0U/maxresdefault.jpg)](https://www.youtube.com/watch?v=MIB5mcXAo0U) | **Таймкоди:** <br> [00:00](https://www.youtube.com/watch?v=MIB5mcXAo0U&t=0s) — Початок та демонстрація <br> [03:32](https://www.youtube.com/watch?v=MIB5mcXAo0U&t=212s) — Налаштування Telegram бота <br> [06:44](https://www.youtube.com/watch?v=MIB5mcXAo0U&t=404s) — Огляд схеми пристрою <br> [08:34](https://www.youtube.com/watch?v=MIB5mcXAo0U&t=514s) — Огляд коду <br> [18:23](https://www.youtube.com/watch?v=MIB5mcXAo0U&t=1103s) — 3D модель корпусу <br> [19:05](https://www.youtube.com/watch?v=MIB5mcXAo0U&t=1145s) — Перелік необхідних компонентів <br> [21:07](https://www.youtube.com/watch?v=MIB5mcXAo0U&t=1267s) — Збірка, прошивка та тест <br> [24:14](https://www.youtube.com/watch?v=MIB5mcXAo0U&t=1454s) — Кінцівка |

</div>

## Опис

Телеграм бот який сповіщає про зникнення та появу електромережі з точністю до хвилини, вміє синхронізуватись з реальним часом та збирає мінімальну статистику.

## Особливості

- Рахує точну кількість часу протягом якого світло було відсутнє
- Має захист від розряду батареї - якщо світло пропало та заряду батареї не вистачило, бот все одно сповістить про появу світла та підрахує час скільки його не було
- Безперебійне живлення для ESP32 - при зникненні електромережі ESP32 одразу переходить на живлення від акумулятора без перезавантаження
- Можливість писати та отримувати повідомлення в загальних групах, щоб сповістити максимальну кількість людей
- Надійне з'єднання з WiFi - ESP32 перевіряє наявність WiFi мережі кожні 30 секунд, якщо вона відсутня мікроконтролер намагається підключитись знову
- Зручний та компактний 3D друкований корпус

## Приклад роботи в Telegram

<p align="center">
  <img src="Images/Photos/Photo_5.jpg" alt="Приклад 1" height="600">
  <img src="Images/Photos/Photo_6.jpg" alt="Приклад 2" height="600">
</p>

## Команди бота

```
/info - Про бота
/status - Стан системи
/set_summer_time - Встановити літній час
/set_winter_time - Встановити зимовий час
/clear_data - Очистити статистику
/restart - Віддалений перезапуск бота
```

Також бот вміє реагувати на запитання: "Світло є чи нема?"

## Налаштування Telegram бота

| 🖥️ Desktop | 📱 Mobile |
| :---: | :---: |
| [Переглянути інструкцію](Docs/Installation%20instruction%20for%20desktop.md) | [Переглянути інструкцію](Docs/Installation%20instruction%20for%20mobile.md) |

## Схема підключення

<img src="Images/Circuit_image.png" alt="Схема" width="100%">

## Необхідні компоненти

<img src="Images/Photos/Photo_1.JPG" alt="Компоненти" width="100%">

| № | Назва | Кількість |
| :---: | :--- | :---: |
| 1 | [Мікроконтролер ESP32 C3 SuperMini](https://tinyurl.com/4h6e2k6n) | 1шт |
| 2 | [Підвищуючий dc-dc перетворювач CKCS BS01](https://tinyurl.com/34p7wcya) | 1шт |
| 3 | [Плата заряду TP4056](https://tinyurl.com/2sxvzadh) | 1шт |
| 4 | Діоди Шотткі 1N5819 | 2шт |
| 5 | Резистор 10kΩ | 1шт |
| 6 | Резистор 20kΩ | 1шт |
| 7 | Шурупи M2×6 | 6шт |
| 8 | [Перемикач KCD11](https://tinyurl.com/mryyff8m) | 1шт |
| 9 | [Холдер для 18650](https://tinyurl.com/mjyb3htm) | 1шт |
| 10 | Акумулятор 18650 ємністю > 1000mAh | 1шт |

## Прошивка

### Вихідний код
Остання версія прошивки `V1.5`: [`Light_Watcher_Firmware_V1.5.ino`](Light_Watcher_Firmware_V1.5/Light_Watcher_Firmware_V1.5.ino)
Зміни тільки у коді, жодних змін в залізі чи інших частинах немає, тому рекомендую прошивати саме її
[Детально про зміни](https://github.com/Stanislav-developer/Light_Watcher/releases/tag/v1.5)

Попередня версія `V1.0`: [`Light_Watcher_Firmware_V1.0.ino`](Light_Watcher_Firmware_V1.0/Light_Watcher_Firmware_V1.0.ino)

**Важливо:** .ino файл після завантаження повинен знаходитись у папці з таким самим іменем.

### Встановіть необхідні компоненти для Arduino IDE

**Ядро ESP32:**
[`esp32 by Espressif Systems`](https://github.com/espressif/arduino-esp32) - або через Board Manager в Arduino IDE

**Бібліотека:**
[`UniversalTelegramBot`](https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot) - або через Library Manager в Arduino IDE

### Налаштування коду

Перед завантаженням прошивки замініть значення на свої(за бажанням):
```cpp
const char* ssid = "YOUR_WIFI";           // Назва WiFi мережі
const char* password = "YOUR_PASSWORD";   // Пароль WiFi
const char* botToken = "YOUR_TOKEN";      // Токен бота
const char* chatId = "YOUR_CHAT_ID";      // Ваш Chat ID
const char* groupId = "GROUP_CHAT_ID";    // Chat ID групи (опціонально)
```
Також ви зможете заповнити їх пізніше у WEB Інтерфейсі конфігурації:

<img src="Images/Photos/Photo_10.jpg" alt="WEB Інтерфейс" width="50%">

## Корпус
### Габарити (ДхШхВ): 90 x 85 x 30 мм
Спеціально розроблений 3D корпус для компактного розміщення всіх компонентів:

<table>
  <tr>
    <td width="50%"><img src="Images/Photos/Photo_7.jpg" width="100%"></td>
    <td width="50%"><img src="Images/Photos/Photo_8.jpg" width="100%"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="Images/Photos/Photo_9.jpg" width="100%"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="Images/Photos/Photo_2.JPG" width="100%"></td>
  </tr>
</table>

### Файли для завантаження
**STL моделі для друку:** [`STL Models`](STL%20Models)  
**CAD файл (Fusion 360):** [`CAD File/Main.f3z`](CAD%20File/Main.f3z)

## Збірка

<img src="Images/Photos/Photo_3.JPG" alt="Збірка" width="80%">

## Готовий пристрій

<p align="center">
  <img src="Images/Photos/Photo_12.jpg" alt="Готовий пристрій 1" height="400">
  <img src="Images/Photos/Photo_11.jpg" alt="Готовий пристрій 2" height="400">
</p>

# 💬 Зворотний зв'язок

Буду вдячний за підтримку цього проєкту! Відкритий до критики, запитань, порад і пропозицій.

[![YouTube](https://img.shields.io/badge/YouTube-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/@TehnoMaisterna) [![GitHub](https://img.shields.io/badge/GitHub-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/Stanislav-developer) [![Telegram](https://img.shields.io/badge/Telegram-26A5E4?style=for-the-badge&logo=telegram&logoColor=white)](https://t.me/@Stanislav5749)
