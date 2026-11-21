<p align="center">
  <img src="docs/avatar_electric_left.png" width="128" height="128" alt="ZenDesktop Logo" style="border-radius: 50%;">
</p>

<h1 align="center">ZenDesktop</h1>

<p align="center">
  <b>Модифицированный кросс-платформенный Telegram клиент с огромным количеством собственного и перенесённого функционала.</b>
</p>

<p align="center">
  <a href="https://github.com/creamblyy/ZenDesktop/actions/workflows/build-zendesktop.yml"><img src="https://github.com/creamblyy/ZenDesktop/actions/workflows/build-zendesktop.yml/badge.svg" alt="Build Status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3"></a>
  <a href="https://github.com/creamblyy/ZenDesktop/releases"><img src="https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg?logo=windows&logoColor=white" alt="Windows x64"></a>
  <a href="https://creamblyy.github.io/ZenDesktop/"><img src="https://img.shields.io/badge/Website-Online-2b3cff" alt="Website"></a>
</p>

---

## О проекте

**ZenDesktop** — форк официального клиента Telegram Desktop, сфокусированный на приватности, расширенном контроле над сессией, эргономике интерфейса в стиле Material 3 Expressive и автономной инфраструктуре обновлений.

Сайт проекта: [creamblyy.github.io/ZenDesktop](https://creamblyy.github.io/ZenDesktop/)

---

## Ключевой функционал

- **RelayHooks Subsystem:**
  - **Anti-Recall:** локальное сохранение удалённых собеседниками сообщений с визуальной индикацией удаления.
  - **Ghost Mode:** полноценный скрытый режим (блокировка отправки сетевых событий `sendTyping` и скрытие отметок прочтения входящих сообщений).
- **ZenBadges:**
  - Система кастомных векторных 8-лепестковых розеток верификации (`desktop`, `dev`, `staff`, `supporter`) с нативным рендерингом в заголовках и чатах.
- **OTA Автообновление:**
  - Встроенный автономный модуль проверки релизов (`Relay::ZenUpdater`), оповещения пользователя о новых версиях и фоновой установки обновлений.
- **Интерфейс и эргономика:**
  - Material 3 Expressive скругления бабблов сообщений (24px для внешних углов, 8px для смежных).
  - Выделенный раздел параметров в меню настроек Telegram.

---

## Установка и релизы

Готовые сборки для Windows x64 доступны на странице [GitHub Releases](https://github.com/creamblyy/ZenDesktop/releases).

1. Перейдите в [Releases](https://github.com/creamblyy/ZenDesktop/releases).
2. Скачайте архив `ZenDesktop-Windows-x64.zip`.
3. Распакуйте и запустите `Telegram.exe`.

---

## Сборка из исходников

### Требования
- Windows 10 / 11 (x64)
- Visual Studio 2022 (MSVC v143, Desktop C++, Windows 10/11 SDK)
- Python 3.10+
- Git, CMake 3.25+, Ninja

### Автоматическая сборка
Сборка выполняется автоматически через GitHub Actions workflow [build-zendesktop.yml](.github/workflows/build-zendesktop.yml). Артефакт `ZenDesktop-Windows-x64` генерируется на чистом runner'е Windows Server 2022 и доступен для скачивания в разделе Actions.

---

## Контрибьюторы

<p align="left">
  <a href="https://github.com/creamblyy">
    <img src="https://github.com/creamblyy.png" width="64" height="64" style="border-radius: 50%;" alt="creamblyy" />
  </a>
  <br>
  <b><a href="https://github.com/creamblyy">@creamblyy</a></b> — создатель, мейнтейнер и ведущий разработчик (Lead Developer / Maintainer).
</p>

---

## Лицензия

Исходный код проекта распространяется под лицензией **GNU General Public License v3.0** с исключением OpenSSL (GPLv3 with OpenSSL Exception). Подробный текст лицензии доступен в файле [LICENSE](LICENSE).
