#include "DevNotifyQtStyle.h"
#include "loggingCategories.h"
#include <QDebug>
#include <thread>
#include <unistd.h>

#include <time.h>

DevNotifyQtStyle::DevNotifyQtStyle(QObject* parent) : QObject(parent)
{
    std::thread udev_thread([this]() {
        udev* udev_ = udev_new();

        auto mon = udev_monitor_new_from_netlink(udev_, "udev");
        //    udev_monitor_filter_add_match_subsystem_devtype(mon, "net", NULL);
        udev_monitor_enable_receiving(mon);
        auto fd = udev_monitor_get_fd(mon);
        QSet<QString> connectedPaths;
        while (1) {
            fd_set fds;
            int ret;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            ret = select(fd + 1, &fds, NULL, NULL, NULL);
            if (ret > 0 && FD_ISSET(fd, &fds)) {
                auto dev = udev_monitor_receive_device(mon);
                if (dev) {
                    char const* action = udev_device_get_action(dev);
                    if (action && !strcmp(action, "add")) {
                        //                    printf("I: ACTION=%s\n", udev_device_get_action(dev));
                        //                    printf("I: DEVNAME=%s\n", udev_device_get_sysname(dev));
                        //                    printf("I: DEVPATH=%s\n", udev_device_get_devpath(dev));
                        //                    printf("I: MACADDR=%s\n", udev_device_get_sysattr_value(dev, "address"));
                        //                    printf("---\n");
                        //                    qDebug() << "addr" << dev <<  "devnode" << udev_device_get_devnode(dev)
                        //                    <<endl<<
                        //                         "sysname" << udev_device_get_sysname(dev) << "devpath" <<
                        //                         udev_device_get_devpath(dev) <<endl<<
                        //                        "devtype" << udev_device_get_devtype(dev) << "subsystem" <<
                        //                        udev_device_get_subsystem(dev)<<endl
                        //                         << "vid" << udev_device_get_sysattr_value(dev, "idVendor")
                        //                         << "pid" << udev_device_get_sysattr_value(dev,
                        //                         "idProduct")<<endl<<"ACTION"<<udev_device_get_action(dev) ;

                        //                    qDebug() << "grandparent vid:" << udev_device_get_sysattr_value(
                        //                                    udev_device_get_parent(udev_device_get_parent(dev)),
                        //                                    "idVendor") << endl << endl;
                        udev_device* grandparent = udev_device_get_parent(udev_device_get_parent(dev));
                        char const* gvid         = udev_device_get_sysattr_value(grandparent, "idVendor");
                        char const* gpid         = udev_device_get_sysattr_value(grandparent, "idProduct");
                        /* free dev */

                        if (gvid && !strcmp(gvid, "03eb") && gpid && !strcmp(gpid, "6124") && action &&
                            !strcmp(action, "add")) {
                            //                            static int count = 0;
                            //                        qDebug() << count++;
                            QString devnode = udev_device_get_devnode(dev);
                            QString grandParentPath =
                                udev_device_get_devpath(udev_device_get_parent(udev_device_get_parent(dev)));

                            qCInfo(cat_common).noquote()
                                << QStringLiteral(
                                       "Обнаружено USB-устройство по паттерну Atmel SAM-BA "
                                       "(idVendor=%1, idProduct=%2). Путь: '%3'. Устройство: '%4'.")
                                       .arg(QString::fromLatin1(gvid), QString::fromLatin1(gpid), grandParentPath,
                                            devnode);
                            QMetaObject::invokeMethod(
                                this, "deviceConnectedOnCable", Q_ARG(QString, grandParentPath),
                                Q_ARG(QString, devnode));
                            connectedPaths.insert(grandParentPath);
                        }
                    } else if (action && !strcmp(action, "remove")) {
                        QString grandParentPath = udev_device_get_devpath(dev);
                        QString devnode         = udev_device_get_devnode(dev);
                        //                        qDebug() << "\n\n";
                        //                            qDebug() << "connectedPaths" << connectedPaths;
                        //                        qDebug() << grandParentPath << devnode << "\n\n";

                        if (connectedPaths.contains(grandParentPath)) {
                            qCInfo(cat_common).noquote()
                                << QStringLiteral("USB-устройство отключено. Путь: '%1'. Устройство: '%2'.")
                                       .arg(grandParentPath, devnode);
                            QMetaObject::invokeMethod(
                                this, "deviceDisconnectedOnCable", Q_ARG(QString, grandParentPath),
                                Q_ARG(QString, devnode));
                            connectedPaths.remove(grandParentPath);
                        }
                    }
                    udev_device_unref(dev);
                }
            }
            //            понять какую прошивку ставить и это всё

            /* 500 milliseconds */
            //            usleep(500*1000);
        }
        /* free udev */
        udev_unref(udev_);
    });
    pthread_setname_np(udev_thread.native_handle(), "udev_thread");
    udev_thread.detach();

    //    while (1) {
    //        udev_device* dev = udev_monitor_receive_device(udev_monitor_);
    //        if(dev) {
    //        }
}
//        fd_set fds;
//        struct timeval tv;
//        int ret;

//        FD_ZERO(&fds);
//        FD_SET(fd, &fds);
//        tv.tv_sec = 0;
//        tv.tv_usec = 0;

//        ret = select(fd+1, &fds, NULL, NULL, &tv);
//        if (ret > 0 && FD_ISSET(fd, &fds)) {
//            dev = udev_monitor_receive_device(mon);
//            if (dev) {
//                printf("I: ACTION=%s\n", udev_device_get_action(dev));
//                printf("I: DEVNAME=%s\n", udev_device_get_sysname(dev));
//                printf("I: DEVPATH=%s\n", udev_device_get_devpath(dev));
//                printf("I: MACADDR=%s\n", udev_device_get_sysattr_value(dev, "address"));
//                printf("---\n");

//                /* free dev */
//                udev_device_unref(dev);
//            }
//        }
//        /* 500 milliseconds */
//        usleep(500*1000);
//    }
//}
