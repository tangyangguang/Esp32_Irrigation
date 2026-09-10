#pragma once

class IrrigationApp;

class IrrigationWeb {
public:
    static bool registerRoutes(IrrigationApp& app);

private:
    static void overview();
    static void activeTask();
    static void plans();
    static void zones();
    static void zoneLearning();
    static void records();
    static void events();
    static void statusApi();
    static void flowHistoryApi();
};
