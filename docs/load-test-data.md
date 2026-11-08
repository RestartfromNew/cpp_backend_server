# 压测数据准备

批量生成测试账号、设备、身份公钥、signed prekey bundle、一次性 prekey 和双向好友关系，使用 CSV / PostgreSQL COPY 导入。

完整脚本、Windows/Linux 命令、真实密钥与占位模式区别，以及登录取得 Token 的步骤，见 [压测数据准备说明](../tests/load/README.md)。

脚本只生成文件，不自动连接数据库或修改服务器逻辑。先使用 10 个账号完成真实导入、登录和握手验证，再扩大规模。
