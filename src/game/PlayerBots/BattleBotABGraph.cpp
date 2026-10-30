/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "BattleBotABGraph.h"
#include <cfloat>
#include <cmath>

namespace ABGraph
{

// ---- Generated from the AB route map (25 hubs, 36 legs). Do not edit by hand. ----
static BattleBotPath vPathAB_AS_SMB =
{
    { 1291.396362f, 1288.405151f, -13.873962f, nullptr },
    { 1251.704834f, 1233.569092f, -36.184464f, nullptr },
    { 1232.233032f, 1187.001831f, -49.803619f, nullptr },
    { 1196.772095f, 1098.692627f, -62.507912f, nullptr },
};

static BattleBotPath vPathAB_SMB_M =
{
    { 1196.772095f, 1098.692627f, -62.507912f, nullptr },
    { 1214.199585f, 1052.643677f, -68.969292f, nullptr },
    { 1210.411133f, 997.512634f, -81.994591f, nullptr },
    { 1179.874878f, 931.664001f, -100.697914f, nullptr },
    { 1140.438354f, 855.528503f, -110.596519f, nullptr },
};

static BattleBotPath vPathAB_M_MBF =
{
    { 1140.438354f, 855.528503f, -110.596519f, nullptr },
    { 1103.094971f, 844.596191f, -106.087746f, nullptr },
    { 1048.518677f, 822.846008f, -92.599113f, nullptr },
    { 1009.383911f, 818.742798f, -79.803925f, nullptr },
    { 961.469421f, 816.843445f, -62.658134f, nullptr },
};

static BattleBotPath vPathAB_AS_SH1 =
{
    { 1291.396362f, 1288.405151f, -13.873962f, nullptr },
    { 1251.795044f, 1254.060425f, -31.076918f, nullptr },
    { 1217.839722f, 1230.288696f, -46.43301f, nullptr },
    { 1179.052124f, 1221.893799f, -53.259789f, nullptr },
};

static BattleBotPath vPathAB_SH1_S =
{
    { 1179.052124f, 1221.893799f, -53.259789f, nullptr },
    { 1165.678711f, 1203.844849f, -56.485683f, nullptr },
};

static BattleBotPath vPathAB_SH1_SBL =
{
    { 1179.052124f, 1221.893799f, -53.259789f, nullptr },
    { 1152.702148f, 1225.019897f, -54.372505f, nullptr },
    { 1118.516724f, 1225.442993f, -47.106514f, nullptr },
    { 1062.198486f, 1214.808228f, -53.413628f, nullptr },
    { 1010.536926f, 1198.315186f, -50.208698f, nullptr },
};

static BattleBotPath vPathAB_S_SBL =
{
    { 1165.678711f, 1203.844849f, -56.485683f, nullptr },
    { 1152.702148f, 1225.019897f, -54.372505f, nullptr },
    { 1118.516724f, 1225.442993f, -47.106514f, nullptr },
    { 1062.198486f, 1214.808228f, -53.413628f, nullptr },
    { 1010.536926f, 1198.315186f, -50.208698f, nullptr },
};

static BattleBotPath vPathAB_SBL_L =
{
    { 1010.536926f, 1198.315186f, -50.208698f, nullptr },
    { 987.716431f, 1214.0271f, -44.809807f, nullptr },
    { 940.612671f, 1226.8396f, -24.607841f, nullptr },
    { 907.322144f, 1205.977417f, -7.401688f, nullptr },
    { 874.661438f, 1186.272339f, 9.794998f, nullptr },
    { 850.846863f, 1154.059082f, 11.544311f, nullptr },
};

static BattleBotPath vPathAB_L_LBF =
{
    { 850.846863f, 1154.059082f, 11.544311f, nullptr },
    { 813.674561f, 1133.502075f, 10.475343f, nullptr },
    { 792.838562f, 1097.523071f, 0.021982f, nullptr },
    { 787.692383f, 1055.662231f, -21.593691f, nullptr },
    { 792.609131f, 1015.821899f, -41.921288f, nullptr },
    { 845.68396f, 980.487061f, -60.030281f, nullptr },
};

static BattleBotPath vPathAB_LBF_F =
{
    { 845.68396f, 980.487061f, -60.030281f, nullptr },
    { 826.779053f, 942.362671f, -58.577736f, nullptr },
    { 811.722229f, 901.468872f, -58.567516f, nullptr },
    { 807.223389f, 877.456238f, -55.993267f, nullptr },
};

static BattleBotPath vPathAB_SMB_SBF =
{
    { 1196.772095f, 1098.692627f, -62.507912f, nullptr },
    { 1174.627197f, 1049.399902f, -64.992203f, nullptr },
    { 1152.720703f, 996.485901f, -63.868259f, nullptr },
};

static BattleBotPath vPathAB_SBF_MBF =
{
    { 1152.720703f, 996.485901f, -63.868259f, nullptr },
    { 1136.190186f, 959.329224f, -62.926826f, nullptr },
    { 1097.071777f, 919.881897f, -62.251961f, nullptr },
    { 1042.497437f, 875.334595f, -62.597317f, nullptr },
    { 984.808716f, 845.064575f, -64.414146f, nullptr },
    { 961.469421f, 816.843445f, -62.658134f, nullptr },
};

static BattleBotPath vPathAB_B_LBF =
{
    { 980.661133f, 1042.472778f, -44.184006f, nullptr },
    { 949.64801f, 1022.603577f, -48.236912f, nullptr },
    { 912.560913f, 1000.399536f, -58.860874f, nullptr },
    { 871.145752f, 988.174988f, -61.875965f, nullptr },
    { 845.68396f, 980.487061f, -60.030281f, nullptr },
};

static BattleBotPath vPathAB_B_SBL =
{
    { 980.661133f, 1042.472778f, -44.184006f, nullptr },
    { 1010.243103f, 1047.568481f, -43.599041f, nullptr },
    { 1029.429932f, 1056.114136f, -50.264935f, nullptr },
    { 1017.481323f, 1094.704712f, -65.879066f, nullptr },
    { 1003.77002f, 1147.3479f, -66.444794f, nullptr },
    { 999.406433f, 1168.819092f, -54.334526f, nullptr },
    { 1010.536926f, 1198.315186f, -50.208698f, nullptr },
};

static BattleBotPath vPathAB_SBL_LBF =
{
    { 1010.536926f, 1198.315186f, -50.208698f, nullptr },
    { 987.443237f, 1184.200562f, -50.366505f, nullptr },
    { 950.980591f, 1153.247559f, -54.359211f, nullptr },
    { 907.694702f, 1110.380737f, -56.927269f, nullptr },
    { 870.169373f, 1058.994629f, -57.541706f, nullptr },
    { 852.375427f, 1010.005615f, -59.324932f, nullptr },
    { 845.68396f, 980.487061f, -60.030281f, nullptr },
};

static BattleBotPath vPathAB_SGY_SH2 =
{
    { 1201.869507f, 1163.130615f, -56.285969f, nullptr },
    { 1190.825195f, 1159.352051f, -56.138786f, nullptr },
};

static BattleBotPath vPathAB_SH2_S =
{
    { 1190.825195f, 1159.352051f, -56.138786f, nullptr },
    { 1172.387817f, 1173.714355f, -56.488224f, nullptr },
    { 1165.678711f, 1203.844849f, -56.485683f, nullptr },
};

static BattleBotPath vPathAB_SH2_SMB =
{
    { 1190.825195f, 1159.352051f, -56.138786f, nullptr },
    { 1193.770142f, 1135.942139f, -56.663448f, nullptr },
    { 1196.772095f, 1098.692627f, -62.507912f, nullptr },
};

static BattleBotPath vPathAB_LGY_L =
{
    { 775.169983f, 1206.400024f, 15.79f, nullptr },
    { 780.239319f, 1189.321655f, 14.018565f, nullptr },
    { 794.421448f, 1160.814331f, 12.015224f, nullptr },
    { 819.961731f, 1147.731201f, 11.047713f, nullptr },
    { 850.846863f, 1154.059082f, 11.544311f, nullptr },
};

static BattleBotPath vPathAB_MBF_FH1 =
{
    { 961.469421f, 816.843445f, -62.658134f, nullptr },
    { 927.161621f, 788.848389f, -55.344181f, nullptr },
    { 884.36731f, 777.731628f, -47.181747f, nullptr },
    { 862.460999f, 781.770813f, -52.385208f, nullptr },
    { 859.190796f, 798.42749f, -55.470833f, nullptr },
};

static BattleBotPath vPathAB_FH1_F =
{
    { 859.190796f, 798.42749f, -55.470833f, nullptr },
    { 859.392456f, 822.001038f, -58.561737f, nullptr },
    { 832.985535f, 858.275818f, -58.123867f, nullptr },
    { 807.223389f, 877.456238f, -55.993267f, nullptr },
};

static BattleBotPath vPathAB_FGY_FH1 =
{
    { 834.726379f, 784.978699f, -57.081944f, nullptr },
    { 859.190796f, 798.42749f, -55.470833f, nullptr },
};

static BattleBotPath vPathAB_MBF_BH1 =
{
    { 961.469421f, 816.843445f, -62.658134f, nullptr },
    { 980.608582f, 861.111084f, -63.67086f, nullptr },
    { 1000.404053f, 918.211426f, -66.0457f, nullptr },
    { 986.006653f, 937.902954f, -49.898376f, nullptr },
    { 994.838623f, 960.185059f, -43.164642f, nullptr },
    { 1011.414185f, 982.77948f, -43.942677f, nullptr },
};

static BattleBotPath vPathAB_BGY_BH1 =
{
    { 1016.588318f, 955.184692f, -42.828693f, nullptr },
    { 1011.414185f, 982.77948f, -43.942677f, nullptr },
};

static BattleBotPath vPathAB_BH1_B =
{
    { 1011.414185f, 982.77948f, -43.942677f, nullptr },
    { 1012.12323f, 1009.57251f, -44.174797f, nullptr },
    { 1000.143311f, 1024.602661f, -43.961544f, nullptr },
    { 980.661133f, 1042.472778f, -44.184006f, nullptr },
};

static BattleBotPath vPathAB_BH1_SBF =
{
    { 1011.414185f, 982.77948f, -43.942677f, nullptr },
    { 1031.665283f, 993.172791f, -44.890572f, nullptr },
    { 1061.058105f, 1001.053772f, -52.498493f, nullptr },
    { 1108.713379f, 1000.801392f, -61.764072f, nullptr },
    { 1141.976196f, 1000.03009f, -63.547844f, nullptr },
    { 1152.720703f, 996.485901f, -63.868259f, nullptr },
};

static BattleBotPath vPathAB_MGY_M =
{
    { 1211.523682f, 781.556946f, -82.709511f, nullptr },
    { 1178.04895f, 787.048828f, -89.735176f, nullptr },
    { 1158.263672f, 813.802917f, -99.942039f, nullptr },
    { 1140.438354f, 855.528503f, -110.596519f, nullptr },
};

static BattleBotPath vPathAB_TBGY_AS =
{
    { 1354.380615f, 1270.12207f, -11.125895f, nullptr },
    { 1329.707886f, 1281.419434f, -12.299872f, nullptr },
    { 1291.396362f, 1288.405151f, -13.873962f, nullptr },
};

static BattleBotPath vPathAB_SBF_B =
{
    { 1152.720703f, 996.485901f, -63.868259f, nullptr },
    { 1141.976196f, 1000.03009f, -63.547844f, nullptr },
    { 1108.713379f, 1000.801392f, -61.764072f, nullptr },
    { 1065.290894f, 1011.108215f, -52.337467f, nullptr },
    { 1020.864685f, 1028.942017f, -44.292816f, nullptr },
    { 980.661133f, 1042.472778f, -44.184006f, nullptr },
};

static BattleBotPath vPathAB_HS_FH2 =
{
    { 702.612366f, 704.188049f, -16.146168f, nullptr },
    { 728.408813f, 739.451233f, -33.558514f, nullptr },
    { 754.186768f, 780.151001f, -45.652283f, nullptr },
    { 768.85144f, 840.876831f, -50.987438f, nullptr },
    { 778.639648f, 864.803467f, -54.160175f, nullptr },
};

static BattleBotPath vPathAB_FH2_F =
{
    { 778.639648f, 864.803467f, -54.160175f, nullptr },
    { 807.223389f, 877.456238f, -55.993267f, nullptr },
};

static BattleBotPath vPathAB_FH2_LBF =
{
    { 778.639648f, 864.803467f, -54.160175f, nullptr },
    { 797.788696f, 901.946228f, -56.715984f, nullptr },
    { 822.660217f, 943.649353f, -58.489182f, nullptr },
    { 845.68396f, 980.487061f, -60.030281f, nullptr },
};

static BattleBotPath vPathAB_HS_FH3 =
{
    { 702.612366f, 704.188049f, -16.146168f, nullptr },
    { 748.110779f, 735.979248f, -35.894707f, nullptr },
    { 788.705261f, 754.520508f, -45.482342f, nullptr },
    { 826.840881f, 764.158997f, -49.528812f, nullptr },
};

static BattleBotPath vPathAB_FH3_FH1 =
{
    { 826.840881f, 764.158997f, -49.528812f, nullptr },
    { 859.190796f, 798.42749f, -55.470833f, nullptr },
};

static BattleBotPath vPathAB_FH3_MBF =
{
    { 826.840881f, 764.158997f, -49.528812f, nullptr },
    { 884.36731f, 777.731628f, -47.181747f, nullptr },
    { 927.161621f, 788.848389f, -55.344181f, nullptr },
    { 961.469421f, 816.843445f, -62.658134f, nullptr },
};

static BattleBotPath vPathAB_DDGY_HS =
{
    { 713.54303f, 641.326355f, -10.606013f, nullptr },
    { 710.939392f, 674.170715f, -15.382189f, nullptr },
    { 702.612366f, 704.188049f, -16.146168f, nullptr },
};

static Hub const aHubs[HUB_COUNT] =
{
    { "AS", 1291.396362f, 1288.405151f, -13.873962f, 255 }, // AS · Início da Aliança
    { "SMB", 1196.772095f, 1098.692627f, -62.507912f, 255 }, // SMB · Hub Stables-Mine-Blacksmith
    { "M", 1140.438354f, 855.528503f, -110.596519f, 4 }, // M · Gold Mine
    { "MBF", 961.469421f, 816.843445f, -62.658134f, 255 }, // MBF · Hub Mine-Blacksmith-Farm
    { "F", 807.223389f, 877.456238f, -55.993267f, 2 }, // F · Farm
    { "SH1", 1179.052124f, 1221.893799f, -53.259789f, 255 }, // SH1 · Hub do Stables
    { "S", 1165.678711f, 1203.844849f, -56.485683f, 0 }, // S · Stables
    { "SBL", 1010.536926f, 1198.315186f, -50.208698f, 255 }, // SBL · Hub Stables-Blacksmith-Lumber
    { "L", 850.846863f, 1154.059082f, 11.544311f, 3 }, // L · Lumber Mill
    { "LBF", 845.68396f, 980.487061f, -60.030281f, 255 }, // LBF · Hub Lumber-Blacksmith-Farm
    { "SBF", 1152.720703f, 996.485901f, -63.868259f, 255 }, // SBF · Hub Stables-Blacksmith-Farm
    { "B", 980.661133f, 1042.472778f, -44.184006f, 1 }, // B · Blacksmith
    { "SGY", 1201.869507f, 1163.130615f, -56.285969f, 255 }, // SGY · Cemitério do Stables
    { "SH2", 1190.825195f, 1159.352051f, -56.138786f, 255 }, // SH2 · Hub Stables 2
    { "LGY", 775.169983f, 1206.400024f, 15.79f, 255 }, // LGY · Cemitério da Lumber Mill
    { "TBGY", 1354.380615f, 1270.12207f, -11.125895f, 255 }, // TBGY · Cemitério de Trollbane Hall (base Aliança)
    { "MGY", 1211.523682f, 781.556946f, -82.709511f, 255 }, // MGY · Cemitério da Mina
    { "BGY", 1016.588318f, 955.184692f, -42.828693f, 255 }, // BGY · Cemitério do Blacksmith
    { "BH1", 1011.414185f, 982.77948f, -43.942677f, 255 }, // BH1 · Hub Blacksmith 1
    { "FGY", 834.726379f, 784.978699f, -57.081944f, 255 }, // FGY · Cemitério da Farm
    { "FH1", 859.190796f, 798.42749f, -55.470833f, 255 }, // FH1 · Hub Farm 1
    { "HS", 702.612366f, 704.188049f, -16.146168f, 255 }, // HS · Início da Horda
    { "FH2", 778.639648f, 864.803467f, -54.160175f, 255 }, // FH2 · Hub Farm 2
    { "FH3", 826.840881f, 764.158997f, -49.528812f, 255 }, // FH3 · Hub Farm 3
    { "DDGY", 713.54303f, 641.326355f, -10.606013f, 255 }, // DDGY · Cemitério do Defiler's Den (base Horda)
};

static Edge const aEdges[] =
{
    { HUB_AS, HUB_SMB, &vPathAB_AS_SMB, true },
    { HUB_SMB, HUB_M, &vPathAB_SMB_M, false },
    { HUB_M, HUB_MBF, &vPathAB_M_MBF, false },
    { HUB_AS, HUB_SH1, &vPathAB_AS_SH1, true },
    { HUB_SH1, HUB_S, &vPathAB_SH1_S, false },
    { HUB_SH1, HUB_SBL, &vPathAB_SH1_SBL, false },
    { HUB_S, HUB_SBL, &vPathAB_S_SBL, false },
    { HUB_SBL, HUB_L, &vPathAB_SBL_L, false },
    { HUB_L, HUB_LBF, &vPathAB_L_LBF, false },
    { HUB_LBF, HUB_F, &vPathAB_LBF_F, false },
    { HUB_SMB, HUB_SBF, &vPathAB_SMB_SBF, false },
    { HUB_SBF, HUB_MBF, &vPathAB_SBF_MBF, false },
    { HUB_B, HUB_LBF, &vPathAB_B_LBF, false },
    { HUB_B, HUB_SBL, &vPathAB_B_SBL, false },
    { HUB_SBL, HUB_LBF, &vPathAB_SBL_LBF, false },
    { HUB_SGY, HUB_SH2, &vPathAB_SGY_SH2, true },
    { HUB_SH2, HUB_S, &vPathAB_SH2_S, false },
    { HUB_SH2, HUB_SMB, &vPathAB_SH2_SMB, false },
    { HUB_LGY, HUB_L, &vPathAB_LGY_L, true },
    { HUB_MBF, HUB_FH1, &vPathAB_MBF_FH1, false },
    { HUB_FH1, HUB_F, &vPathAB_FH1_F, false },
    { HUB_FGY, HUB_FH1, &vPathAB_FGY_FH1, true },
    { HUB_MBF, HUB_BH1, &vPathAB_MBF_BH1, false },
    { HUB_BGY, HUB_BH1, &vPathAB_BGY_BH1, true },
    { HUB_BH1, HUB_B, &vPathAB_BH1_B, false },
    { HUB_BH1, HUB_SBF, &vPathAB_BH1_SBF, false },
    { HUB_MGY, HUB_M, &vPathAB_MGY_M, true },
    { HUB_TBGY, HUB_AS, &vPathAB_TBGY_AS, true },
    { HUB_SBF, HUB_B, &vPathAB_SBF_B, false },
    { HUB_HS, HUB_FH2, &vPathAB_HS_FH2, true },
    { HUB_FH2, HUB_F, &vPathAB_FH2_F, true },
    { HUB_FH2, HUB_LBF, &vPathAB_FH2_LBF, false },
    { HUB_HS, HUB_FH3, &vPathAB_HS_FH3, true },
    { HUB_FH3, HUB_FH1, &vPathAB_FH3_FH1, false },
    { HUB_FH3, HUB_MBF, &vPathAB_FH3_MBF, false },
    { HUB_DDGY, HUB_HS, &vPathAB_DDGY_HS, true },
};


static uint32 const EDGE_COUNT = sizeof(aEdges) / sizeof(aEdges[0]);

namespace
{
    // One way of walking a leg: from one hub to another.
    struct Arc
    {
        uint8 from;
        uint8 to;
        uint32 edge;
        bool reverse;
        float length;
    };

    struct RouteTable
    {
        float length[HUB_COUNT][HUB_COUNT];
        int16 firstArc[HUB_COUNT][HUB_COUNT]; // Index in arcs of the first leg of the shortest route, -1 if none.
        std::vector<Arc> arcs;
    };

    float GetPathLength(BattleBotPath const& path)
    {
        float total = 0.0f;
        for (size_t i = 1; i < path.size(); i++)
        {
            float const dx = path[i].x - path[i - 1].x;
            float const dy = path[i].y - path[i - 1].y;
            float const dz = path[i].z - path[i - 1].z;
            total += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        return total;
    }

    // Shortest routes between all hubs (Floyd-Warshall, 25 hubs), built once.
    RouteTable const& GetRouteTable()
    {
        static RouteTable const table = []()
        {
            RouteTable t;
            for (uint32 i = 0; i < HUB_COUNT; i++)
            {
                for (uint32 j = 0; j < HUB_COUNT; j++)
                {
                    t.length[i][j] = i == j ? 0.0f : FLT_MAX;
                    t.firstArc[i][j] = -1;
                }
            }

            for (uint32 e = 0; e < EDGE_COUNT; e++)
            {
                float const length = GetPathLength(*aEdges[e].pPath);
                t.arcs.push_back({ aEdges[e].from, aEdges[e].to, e, false, length });
                if (!aEdges[e].oneWay)
                    t.arcs.push_back({ aEdges[e].to, aEdges[e].from, e, true, length });
            }

            for (uint32 a = 0; a < t.arcs.size(); a++)
            {
                Arc const& arc = t.arcs[a];
                if (arc.length < t.length[arc.from][arc.to])
                {
                    t.length[arc.from][arc.to] = arc.length;
                    t.firstArc[arc.from][arc.to] = int16(a);
                }
            }

            for (uint32 k = 0; k < HUB_COUNT; k++)
            {
                for (uint32 i = 0; i < HUB_COUNT; i++)
                {
                    if (t.length[i][k] == FLT_MAX)
                        continue;
                    for (uint32 j = 0; j < HUB_COUNT; j++)
                    {
                        if (t.length[k][j] == FLT_MAX)
                            continue;
                        float const length = t.length[i][k] + t.length[k][j];
                        if (length < t.length[i][j])
                        {
                            t.length[i][j] = length;
                            t.firstArc[i][j] = t.firstArc[i][k];
                        }
                    }
                }
            }
            return t;
        }();
        return table;
    }
}

Hub const& GetHub(uint8 hub)
{
    return aHubs[hub];
}

uint8 GetFlagHub(uint8 flagNode)
{
    for (uint8 i = 0; i < HUB_COUNT; i++)
    {
        if (aHubs[i].flagNode == flagNode)
            return i;
    }
    return HUB_NONE;
}

uint8 FindNearestHub(float x, float y, float z, float maxDistance)
{
    uint8 best = HUB_NONE;
    float bestDistance = maxDistance;
    for (uint8 i = 0; i < HUB_COUNT; i++)
    {
        float const dx = aHubs[i].x - x;
        float const dy = aHubs[i].y - y;
        float const dz = aHubs[i].z - z;
        float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

float GetRouteLength(uint8 from, uint8 to)
{
    if (from >= HUB_COUNT || to >= HUB_COUNT)
        return FLT_MAX;
    return GetRouteTable().length[from][to];
}

bool GetNextLeg(uint8 from, uint8 to, BattleBotPath*& pPath, bool& reverse)
{
    if (from >= HUB_COUNT || to >= HUB_COUNT || from == to)
        return false;

    RouteTable const& table = GetRouteTable();
    int16 const arcIndex = table.firstArc[from][to];
    if (arcIndex < 0)
        return false;

    Arc const& arc = table.arcs[arcIndex];
    pPath = aEdges[arc.edge].pPath;
    reverse = arc.reverse;
    return true;
}

bool GetLegEnd(BattleBotPath const* pPath, bool reverse, uint8& hub)
{
    for (uint32 e = 0; e < EDGE_COUNT; e++)
    {
        if (aEdges[e].pPath == pPath)
        {
            hub = reverse ? aEdges[e].from : aEdges[e].to;
            return true;
        }
    }
    return false;
}

bool IsOneWayLeg(BattleBotPath const* pPath)
{
    for (uint32 e = 0; e < EDGE_COUNT; e++)
    {
        if (aEdges[e].pPath == pPath)
            return aEdges[e].oneWay;
    }
    return false;
}

} // namespace ABGraph

// Every leg of the graph (used by the generic path helpers and the debug commands).
std::vector<BattleBotPath*> const vPaths_AB =
{
    &ABGraph::vPathAB_AS_SMB,
    &ABGraph::vPathAB_SMB_M,
    &ABGraph::vPathAB_M_MBF,
    &ABGraph::vPathAB_AS_SH1,
    &ABGraph::vPathAB_SH1_S,
    &ABGraph::vPathAB_SH1_SBL,
    &ABGraph::vPathAB_S_SBL,
    &ABGraph::vPathAB_SBL_L,
    &ABGraph::vPathAB_L_LBF,
    &ABGraph::vPathAB_LBF_F,
    &ABGraph::vPathAB_SMB_SBF,
    &ABGraph::vPathAB_SBF_MBF,
    &ABGraph::vPathAB_B_LBF,
    &ABGraph::vPathAB_B_SBL,
    &ABGraph::vPathAB_SBL_LBF,
    &ABGraph::vPathAB_SGY_SH2,
    &ABGraph::vPathAB_SH2_S,
    &ABGraph::vPathAB_SH2_SMB,
    &ABGraph::vPathAB_LGY_L,
    &ABGraph::vPathAB_MBF_FH1,
    &ABGraph::vPathAB_FH1_F,
    &ABGraph::vPathAB_FGY_FH1,
    &ABGraph::vPathAB_MBF_BH1,
    &ABGraph::vPathAB_BGY_BH1,
    &ABGraph::vPathAB_BH1_B,
    &ABGraph::vPathAB_BH1_SBF,
    &ABGraph::vPathAB_MGY_M,
    &ABGraph::vPathAB_TBGY_AS,
    &ABGraph::vPathAB_SBF_B,
    &ABGraph::vPathAB_HS_FH2,
    &ABGraph::vPathAB_FH2_F,
    &ABGraph::vPathAB_FH2_LBF,
    &ABGraph::vPathAB_HS_FH3,
    &ABGraph::vPathAB_FH3_FH1,
    &ABGraph::vPathAB_FH3_MBF,
    &ABGraph::vPathAB_DDGY_HS,
};
