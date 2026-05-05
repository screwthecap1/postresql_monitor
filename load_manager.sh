#!/bin/bash

# --- НАСТРОЙКИ ПОДКЛЮЧЕНИЯ ---
USER="monitor_user"
DB_MEDIUM="diplom_medium"
DB_BENCH="benchmark_db"
# Укажи здесь пароль своего пользователя monitor_user
export PGPASSWORD='1234'

show_menu() {
    clear
    echo "===================================================="
    echo "          УПРАВЛЕНИЕ ТЕСТОВОЙ НАГРУЗКОЙ             "
    echo "===================================================="
    echo "1) Имитация активных пользователей (Чтение/Запись)"
    echo "2) Создание аномалий (Откаты транзакций / Rollbacks)"
    echo "3) Проверка лимита сессий (Зависшие соединения)"
    echo "4) КОМПЛЕКСНЫЙ СТРЕСС-ТЕСТ (Все сразу)"
    echo "0) Выход"
    echo "----------------------------------------------------"
    echo "Текущий пользователь: $USER"
    read -p "Выберите вариант нагрузки: " choice
}

while true; do
    show_menu
    case $choice in
        1)
            echo "Запуск pgbench на 60 секунд на $DB_BENCH..."
            pgbench -h localhost -U $USER -c 8 -T 60 $DB_BENCH
            read -p "Нажмите Enter, чтобы продолжить..."
            ;;
        2)
            echo "Генерация 100 откатов транзакций в $DB_MEDIUM..."
            for i in {1..100}; do
                psql -h localhost -U $USER -d $DB_MEDIUM -c "BEGIN; INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES (1, 1, 1, 1, now()); ROLLBACK;" > /dev/null 2>&1
                echo -n "."
            done
            echo -e "\nГотово! Проверьте поле 'Откаты (RB)' в приложении."
            read -p "Нажмите Enter, чтобы продолжить..."
            ;;
        3)
            echo "Открываю 10 имитационных сессий на 30 секунд..."
            for i in {1..10}; do
                psql -h localhost -U $USER -d $DB_MEDIUM -c "SELECT pg_sleep(30);" > /dev/null 2>&1 &
            done
            echo "Сессии открыты. Проверьте поле 'Всего сессий' в Qt."
            sleep 2
            ;;
        4)
            echo "ЗАПУСК МАКСИМАЛЬНОЙ НАГРУЗКИ (Стресс-тест)..."
            pgbench -h localhost -U $USER -c 15 -T 45 $DB_BENCH &

            for i in {1..5}; do
                psql -h localhost -U $USER -d $DB_MEDIUM -c "SELECT pg_sleep(45);" > /dev/null 2>&1 &
            done

            for i in {1..30}; do
                psql -h localhost -U $USER -d $DB_MEDIUM -c "BEGIN; INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES (1, 1, 1, 1, now()); ROLLBACK;" > /dev/null 2>&1
                sleep 0.3
            done

            wait
            echo "Комплексный тест завершен."
            read -p "Нажмите Enter, чтобы продолжить..."
            ;;
        0)
            echo "Выход..."
            exit 0
            ;;
        *)
            echo "Неверный выбор. Попробуйте еще раз."
            sleep 1
            ;;
    esac
done
