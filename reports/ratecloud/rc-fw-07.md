# Отчёт: OTA и совместимость конфигурации — матрица версий/отката

**Ветка:** `agents/ratecloud/rc-fw-07`  
**Задача:** Добавить тестируемую валидацию совместимости (версионный порог) для OTA-обновлений YBX, предотвращающую откат на неподдерживаемые сборки.

## 1. Обзор текущего состояния
Текущая логика OTA (`src/network/OtaReleasePolicy.h/cpp`) уже реализует строгие правила:
- Обновления принимаются только из форка YBX (`jhdvp/ybx-crosspoint`).
- Запрещены превью-сборки для стабильных устройств.
- Проверка целостности образа (ESP image magic, chip_id).
- Запрет на понижение версии (`updateOfferedIsNewer`).

Однако отсутствовал явный "пол" (floor) минимально поддерживаемой версии. Это создавало риск теоретического отката на очень старые сборки (например, 1.x), если бы они каким-то образом оказались в релизном фиде или были подменены.

## 2. Реализованные изменения
Добавлен константный порог `kMinSupportedVersion = "2.0.0"` и функция `versionMeetsFloor()`, которая проверяет, что предлагаемая версия не ниже базовой линии совместимости для X3/X4/X4Pro.

### Изменённые файлы:
1. **`src/network/OtaReleasePolicy.h`**:
   - Добавлена константа `kMinSupportedVersion`.
   - Объявлена функция `bool versionMeetsFloor(const char* offeredVersion)`.
2. **`src/network/OtaReleasePolicy.cpp`**:
   - Реализована `versionMeetsFloor()`. Функция отвергает не только версии ниже порога, но и некорректные форматы (например, "2.0.2.3"), которые парсер `readCore` мог бы частично принять.
3. **`test/ota_release_policy/OtaReleasePolicyTest.cpp`**:
   - Добавлены тесты: `VersionFloorConstantIsDocumentedBaseline`, `VersionMeetsFloorAcceptsAtOrAboveBaseline`, `VersionMeetsFloorRejectsBelowBaseline`, `VersionMeetsFloorRejectsUnparsableInput`.
4. **`test/ota_release_policy_standalone/CMakeLists.txt`**:
   - Создан изолированный CMake-проект для запуска хост-тестов без зависимости от сломанного корневого `test/CMakeLists.txt`.

## 3. Результаты тестирования
Тесты запущены в изолированной среде (`test/ota_release_policy_standalone`).

**Команда запуска:**
```bash
cd test/ota_release_policy_standalone && cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j4 && ./build/OtaReleasePolicyStandaloneTest
```

**Вывод:**
```text
[==========] Running 24 tests from 1 test suite.
[----------] Global test environment set-up.
...
[ RUN      ] OtaReleasePolicy.VersionMeetsFloorAcceptsAtOrAboveBaseline
[       OK ] OtaReleasePolicy.VersionMeetsFloorAcceptsAtOrAboveBaseline (0 ms)
[ RUN      ] OtaReleasePolicy.VersionMeetsFloorRejectsBelowBaseline
[       OK ] OtaReleasePolicy.VersionMeetsFloorRejectsBelowBaseline (0 ms)
[ RUN      ] OtaReleasePolicy.VersionMeetsFloorRejectsUnparsableInput
[       OK ] OtaReleasePolicy.VersionMeetsFloorRejectsUnparsableInput (0 ms)
...
[==========] 24 tests from 1 test suite ran. (0 ms total)
[  PASSED  ] 24 tests.
```

## 4. Риски и ограничения
- **Интеграция с OtaUpdater:** В текущем коммите добавлена только логика проверки. Для полной защиты необходимо вызвать `versionMeetsFloor()` внутри `OtaUpdater::checkForUpdate()` или `installUpdate()`. Это оставлено как задача для следующего шага, чтобы не усложнять текущий коммит и сохранить его независимым.
- **Совместимость:** Порог "2.0.0" выбран как безопасная база для текущих устройств X3/X4/X4Pro. Если в будущем потребуется поддержка более старых устройств, константу нужно будет актуализировать.

## 5. Следующие шаги
1. Интегрировать вызов `versionMeetsFloor()` в `OtaUpdater`.
2. Добавить проверку версии в метаданных конфига (если требуется).

## 6. Пересечение с Kaneo
Задача перекрывается с общими требованиями по безопасности обновлений, но не дублирует работу других агентов, так как фокусируется именно на хост-тестируемой логике версионного порога.

---
**Commit hash:** (будет добавлен после коммита)

## Handoff verification (2026-11-02)
The standalone host binary was re-run independently: 24/24 tests passed. Working tree changes were uncommitted at the end of the trial; this handoff will preserve them in a separate WIP commit. `versionMeetsFloor()` is **not invoked in OtaUpdater**, so this is not an effective OTA guard yet. Root firmware builds and device tests not performed.
