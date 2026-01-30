# Light Watcher
## Телеграм бот на esp32 для пінгування світла.

<!-- Тут буде посилання на ютуб відео -->

# Опис:
Телеграм бот який сповіщає про наявність зникнення та появу електромережі з точністю до хвилини, вміє синхронізуватись з реальним часом та збирає мінімальну статистику.

# Особливості:
- Рахує точну кількість часу впробовж якого світло було відсутнє
- Має захист від розряду батареї
    - Таким чином якщо світло пропало та заряду батареї не вистачило, бот всерівно сповістить про появу світла, та підрахує час скільки його не було, одразу коли воно з'явиться
  
- Безперебійне живлення для ESP32
    - Якщо електромережа пропадає, ESP32 одразу переходить на живлення від акумулятору без перезавантаження
  
- Можливість юота писати та отримувати повідомлення з загальниг груп, щоб сповістити максимальну кількість людей
- Надійне з'єднання з WiFi
    - ESP32 перевіряє наявність WiFi мережі кожних 30 секунд, якщо вона відсутня мікроконтролер пробує реконектитись, так забезпечується надійніше з'єднання з мережею
- Зручний та компактний 3Д друкований корпус для пристрою

# Приклад як це виглядає у Telegram:

<p align="lef">
  <img src="Images/Photos/Photo_5.jpg" alt="Circuit_Image" height="600">
  <img src="Images/Photos/Photo_6.jpg" alt="Circuit_Image" height="600">
</p>

## Список команд для бота:
```
/info - Про бота
/status - Стан системи
/set_summer_time - Встановити літній час
/set_winter_time - Встановити зимовий час
/clear_data  - Очистити статистику
/restart - Віддалений перезапуск бота
```

Також бот вміє реагувати на запитання: "Світло є чи нема?"

<!--Тут коротко описуємо налаштування телеграм бота -->


# Cхема:
<img src="Images/Circuit_image.png" alt="Circuit_Image" width="100%">

# Код:

Вихідний код знаходиться у репозиторії за таким шляхом: [`Light_Watcher_Firmware_V1.0/Light_Watcher_Firmware_V1.0.ino`](Light_Watcher_Firmware_V1.0/Light_Watcher_Firmware_V1.0.ino)

Завантажте цей файл або просто скопіюйте у вже існуючому .ino файл у себе на комп'ютері(Важливо завантажений ino. файл потрібно помістити у папку з таким самим іменем).

Також у Arduino IDE вам потрібно завантажити ядро для роботи з платами esp32:   
[`esp32 by Espressif Systems`](https://github.com/espressif/arduino-esp32/releases/latest) _(або через Board Manager)_

Та 1 додаткову бібліотеку:  
[`UniversalTelegramBot`](https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/releases/latest) _(або через Library Manager)_

Перед завантаженням прошивки замініть значення у цих рядках в коді на ваші:
```
const char* ssid = " "; // Назва домашньої WiFi мережі
const char* password = " "; // Пароль домашньої WiFi мережі
const char* botToken = " "; // Токен бота
const char* chatId = " "; // ChatID Власника бота
const char* groupId = " "; // ChatID Загальної групи
```

# Корпус:
Змоделював корпус для того щоб зручно умістити та заховати у ньому всі компоненти:

<img src="Images/Photos/Photo_7.jpg" alt="Circuit_Image" width="100%">
<img src="Images/Photos/Photo_8.jpg" alt="Circuit_Image" width="80%">
<img src="Images/Photos/Photo_9.jpg" alt="Circuit_Image" width="100%">

STL Файли для друку: 
[`Main/STL Models`]
(Main/STL Models)

CAD Файл(Fusion) для редагування: [`CAD File/Main.f3z`](Light_Watcher_Firmware_V1.0/CAD File/Main.f3z)