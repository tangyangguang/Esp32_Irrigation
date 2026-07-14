#pragma once

class IrrigationApp;

class IrrigationWeb {
public:
    static bool registerRoutes(IrrigationApp& app);

private:
    static void overview();
    static void run();
    static void plans();
    static void zones();
    static void records();
    static void settings();
    static void statusApi();
    static void recordsCsv();
};
