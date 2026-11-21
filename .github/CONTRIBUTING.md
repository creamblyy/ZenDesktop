# Участие в разработке ZenDesktop (Contributing)

Мы приветствуем вклад в развитие **ZenDesktop** — открытого форка Telegram Desktop с фокусом на приватность, расширенный контроль и чистую эстетику интерфейса.

---

## Основные правила

1. **Основная ветка разработки** — `dev`. Все пулл-реквесты должны создаваться в ветку `dev`, а не в `master`.
2. **Чистота кода**: придерживайтесь стиля кодовой базы Telegram Desktop и C++20 (четкая типизация, отсутствие предупреждений компилятора MSVC, форматирование отступов в 4 пробела).
3. **Атомарность**: один pull request — одно конкретное исправление или фича. Не смешивайте рефакторинг оформления с логическими изменениями.
4. **Тестирование**: перед отправкой PR убедитесь, что код компилируется без ошибок локально или в GitHub Actions.

---

## Процесс создания Pull Request

1. Сделайте форк репозитория: `https://github.com/creamblyy/ZenDesktop`.
2. Клонируйте свой форк и создайте ветку для новой функциональности:
   ```bash
   git checkout -b feature/your-feature-name origin/dev
   ```
3. Внесите изменения и зафиксируйте их с понятными commit message:
   ```bash
   git commit -m "feat(module): brief description of change"
   ```
4. Отправьте изменения в свой форк:
   ```bash
   git push origin feature/your-feature-name
   ```
5. Откройте Pull Request в ветку `dev` основного репозитория `creamblyy/ZenDesktop`.

---

## Архитектура модулей ZenDesktop

- `Telegram/SourceFiles/relay_hooks/` — ядро перехвата и хуков (Anti-Recall, Ghost Mode, ZenUpdater, ZenBadges).
- `Telegram/SourceFiles/settings/sections/settings_zen.cpp` — секция настроек ZenDesktop в интерфейсе.
- `Telegram/SourceFiles/history/view/` — отрисовка сообщений и скругления бабблов (Material 3 Expressive).

---

## Сообщения об ошибках и предложения (Issues)

Если вы обнаружили баг или хотите предложить улучшение:
1. Проверьте открытые и закрытые [Issues](https://github.com/creamblyy/ZenDesktop/issues), чтобы избежать дубликатов.
2. Подробно опишите сценарий воспроизведения, окружение (версия Windows, билд клиента) и ожидаемое поведение.
3. Приложите скриншот или лог, если это применимо.

---

## Мейнтейнер

- [@creamblyy](https://github.com/creamblyy) — автор и ведущий разработчик проекта.
