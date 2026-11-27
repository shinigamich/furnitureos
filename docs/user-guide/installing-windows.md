# Встановлення FurnitureOS у Windows / Installing FurnitureOS on Windows

[Українська](#українська) · [English](#english)

---

## Українська

FurnitureOS — безкоштовна програма для проєктування меблів. Це **тестова версія**: нею можна
користуватися й пробувати, але розміри фурнітури та програми для верстатів з ЧПК ще не перевірені —
перед виробництвом усе треба перевіряти вручну.

Потрібно: Windows 10 або 11 (64-бітна). Права адміністратора **не потрібні**.

### 1. Завантаження

1. Відкрийте сторінку випусків: <https://github.com/shinigamich/furnitureos/releases>
2. У найновішому випуску, у розділі **Assets**, натисніть файл, що закінчується на
   **`-windows-x64-setup.exe`** (наприклад `FurnitureOS-0.1.0-windows-x64-setup.exe`).
3. Браузер може попередити, що файл «завантажують нечасто». Виберіть **Зберегти** / **Keep**
   (у Edge: «…» ▸ **Зберегти** ▸ **Показати більше** ▸ **Усе одно зберегти**).

### 2. Встановлення

1. Двічі клацніть завантажений файл.
2. З'явиться синє вікно **«Windows захистив ваш ПК»** (Microsoft Defender SmartScreen). Це тому, що
   програма нова й не має платного цифрового підпису, а не тому, що в ній знайдено вірус.
   Натисніть **Докладніше**, потім кнопку **Усе одно запустити**.
3. Виберіть мову, натисніть **Далі**. За бажанням поставте позначку «Створити значок на робочому столі».
4. Натисніть **Встановити**, потім **Завершити**.

Програма встановлюється лише для вашого користувача в папку
`%LOCALAPPDATA%\Programs\FurnitureOS`. Її можна запустити з меню **Пуск** ▸ **FurnitureOS**. Файли
проєктів `.fosp` відкриваються подвійним клацанням.

### 3. Оновлення

Завантажте новий `…-setup.exe` і запустіть його так само — він оновить програму, ваші проєкти та
налаштування залишаться. Закрийте FurnitureOS перед оновленням (інакше інсталятор закриє її сам).

### 4. Видалення

**Пуск** ▸ **Параметри** ▸ **Програми** ▸ **Інстальовані програми** ▸ **FurnitureOS** ▸ «…» ▸
**Видалити**. Ваші файли проєктів не видаляються.

### Якщо щось не працює

* **3D-вигляд порожній або програма закривається одразу**: старий або віртуальний комп'ютер може не
  мати OpenGL 3.3. Встановіть свіжий драйвер відеокарти. Якщо не допомогло — повідомте нам.
* **Без встановлення**: замість інсталятора можна завантажити `…-windows-x64.zip`, розпакувати
  папку будь-куди та запускати `furnitureos.exe`.
* **Перевірка файлу (необов'язково)**: контрольні суми є у файлі `SHA256SUMS.txt` того ж випуску.
  У PowerShell: `Get-FileHash .\FurnitureOS-0.1.0-windows-x64-setup.exe` — число має збігатися.

---

## English

FurnitureOS is a free program for designing furniture. This is a **test version**: you can use it and
try it out, but hardware dimensions and CNC machine programs are not verified yet — check everything by
hand before making anything.

You need Windows 10 or 11 (64-bit). Administrator rights are **not** needed.

### 1. Download

1. Open the releases page: <https://github.com/shinigamich/furnitureos/releases>
2. In the newest release, under **Assets**, click the file ending in **`-windows-x64-setup.exe`**
   (for example `FurnitureOS-0.1.0-windows-x64-setup.exe`).
3. The browser may warn that the file "isn't commonly downloaded". Choose **Keep**
   (in Edge: "…" ▸ **Keep** ▸ **Show more** ▸ **Keep anyway**).

### 2. Install

1. Double-click the downloaded file.
2. A blue **"Windows protected your PC"** window appears (Microsoft Defender SmartScreen). It appears
   because the program is new and has no paid digital signature, not because anything harmful was found.
   Click **More info**, then **Run anyway**.
3. Choose the language and click **Next**. Tick "Create a desktop shortcut" if you want one.
4. Click **Install**, then **Finish**.

The program is installed for your user only, in `%LOCALAPPDATA%\Programs\FurnitureOS`. Start it from the
**Start** menu ▸ **FurnitureOS**. Project files (`.fosp`) open with a double-click.

### 3. Update

Download the new `…-setup.exe` and run it the same way; it updates the program and keeps your projects and
settings. Close FurnitureOS first (otherwise the installer closes it for you).

### 4. Uninstall

**Start** ▸ **Settings** ▸ **Apps** ▸ **Installed apps** ▸ **FurnitureOS** ▸ "…" ▸ **Uninstall**.
Your project files are not deleted.

### If something does not work

* **The 3D view is empty or the program closes at once**: old or virtual machines may lack OpenGL 3.3.
  Install a current graphics driver. If that does not help, please tell us.
* **Without installing**: download `…-windows-x64.zip` instead, unzip the folder anywhere and run
  `furnitureos.exe`.
* **Checking the download (optional)**: the checksums are in `SHA256SUMS.txt` of the same release. In
  PowerShell: `Get-FileHash .\FurnitureOS-0.1.0-windows-x64-setup.exe` — the value must match.
