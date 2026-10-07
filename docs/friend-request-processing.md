# 处理好友申请

POST `/process_friendship_request`，需要 access token（当前中间件使用小写 `authorization`）。

```json
{"request_id":"申请记录UUID","process":"accepted"}
```

不提交 requester_id 或 friend_id。操作者来自认证上下文，申请双方来自数据库。

| process | 操作人 | 效果 |
|---|---|---|
| accepted | 接收者 | 更新申请并在同一个事务创建双向好友关系 |
| rejected | 接收者 | 更新申请，不创建关系 |
| cancelled | 申请者 | 更新申请，不创建关系 |

处理事务先对申请执行 SELECT FOR UPDATE，串行化同一申请的并发操作。
重复提交相同最终状态返回 200、already_processed=true，不再次建立关系；
申请已处于另一最终状态则返回 409。先检查权限，再判断是否重复。
不会因重放已接受申请而恢复后来删除的好友关系。

## 响应

- 200：request_id、status、already_processed。
- 400：空 Body、非法 JSON/UUID/字段/操作。
- 401：缺少或无效 token，由认证中间件处理。
- 403：不是该操作的授权用户。
- 404：申请不存在。
- 409：申请已经以另一状态处理。
- 503：数据库连接错误。
- 500：其他数据库错误；不向客户端暴露 SQL 细节。

## 前提和范围

app.friend_request 具有 requester_id、recipient_id、status、processed_at。
app.friend_relation 不再具有 status，并有 UNIQUE(user_id, friend_id)。
本次没有执行数据库迁移，也没有增加实时通知、账号禁用或屏蔽校验；
这些规则需与账号、屏蔽模块进一步集成，不应视为已经覆盖。

## 手动验收（只在隔离测试环境运行）

每个终态场景使用独立 pending 申请，A 为发起者、B 为接收者、C 为无关用户。

1. B accepted：200；申请 accepted，processed_at 非空；双方各一条关系。
2. B 重复 accepted：200，already_processed=true；记录数量不增加。
3. B 对同一申请 rejected：409；数据库保持 accepted。
4. 另一申请 B rejected：200；没有新增好友关系。
5. 另一申请 A cancelled：200；没有新增好友关系。
6. A accepted、B cancelled、C 任意合法操作：403，无数据变化。
7. 不存在的申请 UUID：404；非法 UUID、非字符串字段、process=pending：400。
8. 两个并发 accepted：均 200，只有一个首次成功；好友关系不重复。
9. 同一申请并发 accepted/rejected：一项 200，另一项 409；最终状态与关系一致。
10. 隔离测试库中模拟好友插入失败：申请更新必须回滚为 pending。
11. 删除已建立关系后重放旧 accepted：返回已处理，不恢复关系。

收到 500/503 时可能无法判断事务是否已提交；用相同申请 ID 和操作重试，
不要创建新申请来代替这次操作。

PowerShell 示例（$bobToken 为 B 的有效 access token）：

```powershell
$processBody = @{
    request_id = "替换成申请ID，不是用户ID"
    process = "accepted"
} | ConvertTo-Json -Compress

Invoke-RestMethod -Method Post `
    -Uri "http://localhost:8082/process_friendship_request" `
    -Headers @{ authorization = "Bearer $bobToken" } `
    -ContentType "application/json" -Body $processBody
```
