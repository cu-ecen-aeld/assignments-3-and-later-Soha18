#!/bin/sh


DAEMON_PATH="/usr/bin/aesdsocket"
DAEMON_NAME="asdsocket"
PIDFILE="/var/run/$DAEMON_NAME.pid"
LOGFILE="/var/log/$DAEMON_NAME.log"

start() {
    echo "Starting $DAEMON_NAME..."
    start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON_PATH -- -d
    if [ $? -eq 0 ]; then
        echo "$DAEMON_NAME started successfully."
    else
        echo "Failed to start $DAEMON_NAME."
    fi
}

stop() {
    echo "Stopping $DAEMON_NAME..."
    start-stop-daemon --stop --quiet --pidfile $PIDFILE --exec $DAEMON_PATH --signal SIGTERM
    if [ $? -eq 0 ]; then
        echo "$DAEMON_NAME stopped successfully."
    else
        echo "Failed to stop $DAEMON_NAME."
    fi
}

restart() {
    stop
    sleep 1
    start
}

status() {
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")
        if ps -p $PID > /dev/null 2>&1; then
            echo "$DAEMON_NAME is running (PID: $PID)."
            exit 0
        else
            echo "$DAEMON_NAME is not running but PID file exists."
            exit 1
        fi
    else
        echo "$DAEMON_NAME is not running."
        exit 3
    fi
}

case "$1" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    restart)
        restart
        ;;
    status)
        status
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
        ;;
esac

exit 0

