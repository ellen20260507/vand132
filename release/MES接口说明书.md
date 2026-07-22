# ESD-1000 静电监控系统 — MES 数据查询接口说明书

**版本**：V1.0  
**适用软件**：静电管理在线监控系统 ESD-1000  
**更新日期**：2026-07-06

---

## 1. 概述

本接口用于客户 **MES / 上位机** 从 ESD-1000 监控电脑读取已采集的测点历史数据。

- 接口由 ESD-1000 程序 **内置 HTTP 服务** 提供，**不经过云服务器**。
- 客户系统通过局域网访问运行 `vand1.exe` 的电脑 IP 即可。
- 数据来源于本机 MySQL 数据库中的 `channel_reading` 表（按轮询配置写入的逐点采集记录）。
- 当前提供 **1 个接口**：按条件查询测点读数。

---

## 2. 网络与部署

| 项目 | 说明 |
|------|------|
| 协议 | HTTP |
| 默认端口 | `1388`（可在程序目录 `server.ini` 的 `[Server] port=` 修改） |
| 监听地址 | 默认 `0.0.0.0`（本机所有网卡均可访问） |
| 基础 URL | `http://{ESD电脑IP}:{端口}` |
| 示例 | `http://192.168.1.100:1388` |

**前置条件**

1. ESD-1000 程序已启动且 HTTP 服务正常运行。  
2. 客户电脑与 ESD 电脑网络互通（可 `ping` 通）。  
3. 若配置了 API Key，请求须携带正确的 `X-Api-Key` 头。  
4. ESD 电脑 MySQL 数据库连接正常（程序能正常采集并入库）。

---

## 3. 鉴权

在 ESD 程序 **exe 同目录** 下的 `mes_api.ini` 中配置：

```ini
[MesApi]
apiKey=您与现场约定的密钥
```

| 配置 | 行为 |
|------|------|
| `apiKey` 留空 | 不校验密钥（仅建议内网调试） |
| `apiKey` 已填写 | 每次请求必须在 HTTP 头携带 `X-Api-Key`，且与配置完全一致 |

**请求头示例**

```
Content-Type: application/json; charset=utf-8
X-Api-Key: 您与现场约定的密钥
```

> API Key 为固定字符串，由现场安装人员写入 `mes_api.ini` 后告知 MES 方，**不会自动递增或轮换**。

---

## 4. 接口：查询测点读数

### 4.1 基本信息

| 项目 | 值 |
|------|-----|
| 路径 | `/api/mes/readings/query` |
| 方法 | `POST` |
| Content-Type | `application/json; charset=utf-8` |
| 完整 URL 示例 | `http://192.168.1.100:1388/api/mes/readings/query` |

### 4.2 请求体（JSON）

```json
{
  "requestId": "MES-20260706-0001",
  "timeRange": {
    "start": "2026-07-01T00:00:00",
    "end": "2026-07-06T23:59:59"
  },
  "devices": {
    "modbusAddrs": "ALL"
  },
  "types": {
    "values": "ALL"
  },
  "channels": {
    "values": "ALL"
  },
  "page": 1,
  "pageSize": 1000
}
```

### 4.3 请求字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `requestId` | string | 否 | 客户自定义请求流水号；原样回显，便于对账与日志关联 |
| `timeRange` | object | 否 | 采集时间范围；省略或字段为空表示不限制 |
| `timeRange.start` | string / null | 否 | 开始时间（含）；见 [4.5 时间格式](#45-时间格式) |
| `timeRange.end` | string / null | 否 | 结束时间（含）；见 [4.5 时间格式](#45-时间格式) |
| `devices` | object | 否 | 设备（Modbus 地址）筛选 |
| `devices.modbusAddrs` | `"ALL"` 或 int[] | 否 | 省略、`null`、`"ALL"` 表示全部**已启用**设备；数组示例：`[1, 25864]` |
| `types` | object | 否 | 测点类型筛选 |
| `types.values` | `"ALL"` 或 string[] | 否 | 省略、`null`、`"ALL"` 表示全部类型；数组元素：`W` `T` `E` `C` `I` |
| `channels` | object | 否 | 通道号筛选（轮询配置中的通道序号） |
| `channels.values` | `"ALL"` 或 int[] | 否 | 省略、`null`、`"ALL"` 表示全部通道；数组示例：`[1, 2, 3]` |
| `page` | int | 否 | 页码，从 `1` 开始；默认 `1`；仅数字分页时有效 |
| `pageSize` | int 或 `"ALL"` | 否 | 每页条数；省略默认 `1000`；**无上限**；`"ALL"` 表示不分页一次返回全部 |

**测点类型含义**

| 类型 | 含义 |
|------|------|
| `W` | 腕带 |
| `T` | 台垫 |
| `E` | 设备接地 |
| `C` | 洁净度（尘埃粒子等） |
| `I` | 离子风机 |

> **关于 `ALL`**：`devices` / `types` / `channels` 的 `ALL` 指系统中**已配置且已启用**的测点范围，不是固定的 1～15 通道。实际范围以 ESD 程序「轮询设置」为准。

### 4.4 分页规则

| `pageSize` | 行为 |
|------------|------|
| 省略 | 每页 1000 条 |
| 正整数 N | 每页 N 条，无上限；配合 `page` 翻页 |
| `"ALL"` | **不分页**，一次返回符合条件的全部记录；忽略 `page` |

**数字分页拉取全量示例**

1. 首次请求：`page=1`, `pageSize=5000`  
2. 响应中 `total` 为总条数；若 `items` 条数小于 `pageSize`，说明已是最后一页  
3. 否则 `page` 加 1 继续请求，直到取完

**一次全量示例**

```json
{
  "requestId": "MES-FULL-001",
  "timeRange": {
    "start": "2026-07-06T08:00:00",
    "end": "2026-07-06T18:00:00"
  },
  "pageSize": "ALL"
}
```

> 数据量很大时，建议使用时间范围拆分或数字分页，避免单次响应过大、超时或占用过多内存。

### 4.5 时间格式

以下格式均可识别（推荐 ISO 8601）：

- `2026-07-06T14:30:00`
- `2026-07-06 14:30:00`
- `2026-07-06 14:30:00.000`

`start` / `end` 传 `null`、空字符串或省略该字段，表示该侧不限制。

---

## 5. 成功响应

**HTTP 状态码**：`200`

```json
{
  "success": true,
  "requestId": "MES-20260706-0001",
  "query": {
    "timeRange": {
      "start": "2026-07-01T00:00:00",
      "end": "2026-07-06T23:59:59"
    },
    "devices": { "modbusAddrs": "ALL" },
    "types": { "values": "ALL" },
    "channels": { "values": "ALL" },
    "page": 1,
    "pageSize": 1000
  },
  "total": 15230,
  "page": 1,
  "pageSize": 1000,
  "items": [
    {
      "pointId": "W25864-1",
      "recordTime": "2026-07-06T08:15:32",
      "valueNum": 1250.0,
      "statusDesc": "正常"
    },
    {
      "pointId": "I25864-1",
      "recordTime": "2026-07-06T08:15:35",
      "valueNum": null,
      "statusDesc": "在线"
    }
  ]
}
```

### 5.1 响应字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | bool | 固定为 `true` |
| `requestId` | string | 与请求一致（请求有传时才返回） |
| `query` | object | 服务端实际使用的查询条件回显 |
| `total` | int | 符合条件的总记录数 |
| `page` | int | 当前页码；`pageSize=ALL` 时为 `1` |
| `pageSize` | int 或 `"ALL"` | 当前每页大小；全量时为 `"ALL"` |
| `items` | array | 本页/本次返回的数据列表 |

### 5.2 `items` 单条记录字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `pointId` | string | 测点唯一标识，与系统轮询配置一致 |
| `recordTime` | string | 采集时间（ISO 8601） |
| `valueNum` | number / null | 数值型读数；无数值时为 `null` |
| `statusDesc` | string | 状态文字描述，见下表 |

**`pointId` 命名规则**

| 类型 | 格式 | 示例 |
|------|------|------|
| W / T / E / I | `{类型}{Modbus地址}-{通道号}` | `W25864-1`、`T1-3`、`I25864-1` |
| C（洁净度主机） | `C{Modbus地址}` | `C25864` |
| C（分项指标） | `C{Modbus地址}_{指标}` | `C25864_PM25`（以现场配置为准） |

**`statusDesc` 常见取值**

| 类型 | 典型取值 | 说明 |
|------|----------|------|
| W / T / E | `正常` / `异常` | 根据阈值判定 |
| I | `在线` / `待机（无回复）` | 有回复为在线，超时无回复为待机 |
| C | 常为空字符串 | 洁净度以 `valueNum` 为主 |

**`valueNum` 说明**

- W / T / E：一般为寄存器解析后的数值（与界面/入库一致）。  
- I：状态类记录可能无数值，此时为 `null`。  
- C：有粒子数等指标时为数值，否则可能为 `null`。

### 5.3 排序规则

`items` 按以下顺序升序排列：

1. `recordTime`（采集时间）  
2. Modbus 地址  
3. 测点类型  
4. 通道号  
5. 记录 ID  

---

## 6. 错误响应

错误时 `success` 为 `false`，并返回 `code` 与 `message`。若请求带了 `requestId`，错误响应中也会回显。

### 6.1 HTTP 状态码

| 状态码 | 含义 |
|--------|------|
| 400 | 请求参数或 JSON 格式错误 |
| 401 | API Key 无效或未提供 |
| 500 | 服务端错误（如数据库不可用、查询失败） |

### 6.2 错误码一览

| code | 说明 |
|------|------|
| `INVALID_API_KEY` | API Key 无效或未提供 |
| `INVALID_JSON` | 请求体不是合法 JSON 对象 |
| `INVALID_TIME_RANGE` | 时间格式无效，或开始时间晚于结束时间 |
| `INVALID_DEVICES` | `devices.modbusAddrs` 格式错误 |
| `INVALID_TYPES` | `types.values` 含不支持类型或格式错误 |
| `INVALID_CHANNELS` | `channels.values` 格式错误 |
| `INVALID_PAGINATION` | `pageSize` 不是正整数或 `ALL` |
| `DB_UNAVAILABLE` | 数据库连接不可用 |
| `QUERY_FAILED` | 数据库查询执行失败 |

**错误响应示例**

```json
{
  "success": false,
  "code": "INVALID_TIME_RANGE",
  "message": "timeRange.start 时间格式无效",
  "requestId": "MES-20260706-0001"
}
```

---

## 7. 调用示例

### 7.1 cURL（Windows / Linux）

**按时间 + 指定设备分页查询**

```bash
curl -X POST "http://192.168.1.100:1388/api/mes/readings/query" ^
  -H "Content-Type: application/json; charset=utf-8" ^
  -H "X-Api-Key: your-secret-key" ^
  -d "{\"requestId\":\"test-001\",\"timeRange\":{\"start\":\"2026-07-06T00:00:00\",\"end\":\"2026-07-06T23:59:59\"},\"devices\":{\"modbusAddrs\":[25864]},\"types\":{\"values\":[\"W\",\"T\"]},\"channels\":{\"values\":[1,2]},\"page\":1,\"pageSize\":500}"
```

（Linux / macOS 将 `^` 换为 `\` 续行。）

**一次拉取某时段全部数据**

```bash
curl -X POST "http://192.168.1.100:1388/api/mes/readings/query" \
  -H "Content-Type: application/json; charset=utf-8" \
  -H "X-Api-Key: your-secret-key" \
  -d '{"requestId":"test-full","timeRange":{"start":"2026-07-06T08:00:00","end":"2026-07-06T09:00:00"},"pageSize":"ALL"}'
```

### 7.2 C#（HttpClient 片段）

```csharp
var client = new HttpClient();
client.DefaultRequestHeaders.Add("X-Api-Key", "your-secret-key");

var body = new {
    requestId = "MES-001",
    timeRange = new { start = "2026-07-06T00:00:00", end = "2026-07-06T23:59:59" },
    devices = new { modbusAddrs = "ALL" },
    types = new { values = new[] { "W", "T", "E" } },
    channels = new { values = "ALL" },
    page = 1,
    pageSize = 2000
};

var json = JsonSerializer.Serialize(body);
var content = new StringContent(json, Encoding.UTF8, "application/json");
var response = await client.PostAsync(
    "http://192.168.1.100:1388/api/mes/readings/query", content);
var result = await response.Content.ReadAsStringAsync();
```

### 7.3 Java（OkHttp 片段）

```java
OkHttpClient client = new OkHttpClient();
String json = "{"
    + "\"requestId\":\"MES-001\","
    + "\"timeRange\":{\"start\":\"2026-07-06T00:00:00\",\"end\":\"2026-07-06T23:59:59\"},"
    + "\"pageSize\":\"ALL\""
    + "}";

Request request = new Request.Builder()
    .url("http://192.168.1.100:1388/api/mes/readings/query")
    .addHeader("Content-Type", "application/json; charset=utf-8")
    .addHeader("X-Api-Key", "your-secret-key")
    .post(RequestBody.create(json, MediaType.parse("application/json")))
    .build();

Response response = client.newCall(request).execute();
```

---

## 8. 联调建议

1. **连通性**：浏览器访问 `http://{IP}:{端口}/static/index.html`（若已部署）或先用 cURL 发一条最小请求。  
2. **先小范围**：建议先限定 `timeRange`（如 1 小时）和少量设备，确认字段含义后再扩大。  
3. **记录 requestId**：便于与 ESD 方日志对照。  
4. **定时拉取**：MES 可按固定周期（如每 5 分钟）查询「上次成功时间～当前时间」增量数据。  
5. **防火墙**：确保 ESD 电脑 Windows 防火墙或杀毒软件放行 `1388` 端口（或实际配置端口）。

---

## 9. 常见问题

**Q：接口是公网地址吗？**  
A：不是。接口只在 ESD 监控电脑本机 HTTP 服务上，通常通过工厂局域网访问。

**Q：没有数据返回？**  
A：检查：① 时间范围是否覆盖采集时段；② 设备 Modbus 地址是否在轮询配置中且已启用；③ 程序是否已完成采集入库。

**Q：`devices.modbusAddrs` 填 `ALL` 会返回未配置的设备吗？**  
A：不会。仅返回轮询配置里**已启用**的设备测点。

**Q：`pageSize` 可以填 100 万吗？**  
A：可以，系统不做上限截断；但单次过大可能导致响应慢或超时，请按实际情况选择分页或 `ALL`。

**Q：API Key 在哪里改？**  
A：修改 ESD 程序目录下 `mes_api.ini` 后**重启程序**生效。

**Q：端口可以改吗？**  
A：可以。修改 `server.ini` 中 `port=` 后重启程序，并同步告知 MES 方新端口。

---

## 10. 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| V1.0 | 2026-07-06 | 首版：测点读数查询接口；支持 `pageSize` 无上限及 `"ALL"` 全量返回 |

---

**技术支持**：请联系 ESD-1000 现场实施人员或设备供应商。
