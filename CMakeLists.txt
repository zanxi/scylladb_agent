cmake_minimum_required(VERSION 3.16)
project(scylla_ws_client VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network WebSockets Charts)

add_executable(scylla_ws_client WIN32 MACOSX_BUNDLE
    main.cpp
    CqlUtil.h CqlUtil.cpp
    WsClient.h WsClient.cpp
    SchemaModel.h SchemaModel.cpp
    ResultModel.h ResultModel.cpp
    CellHoverPopup.h CellHoverPopup.cpp
    CqlHighlighter.h CqlHighlighter.cpp
    LogWidget.h LogWidget.cpp
    LoginDialog.h LoginDialog.cpp
    TableDialogs.h TableDialogs.cpp
    ChartDialog.h ChartDialog.cpp
    MainWindow.h MainWindow.cpp
)

target_link_libraries(scylla_ws_client PRIVATE
    Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Charts)

install(TARGETS scylla_ws_client
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .)
