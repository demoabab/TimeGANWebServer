#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    MYSQL *con = mysql_init(NULL);
    if (con == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        return 1;
    }

    // 这里填你的配置！
    // 如果 localhost 不行，就试 127.0.0.1
    if (mysql_real_connect(con, "localhost", "root", "root", 
                           "tinywebserver", 3306, NULL, 0) == NULL) {
        fprintf(stderr, "连接失败! 错误原因: %s\n", mysql_error(con));
        mysql_close(con);
        return 1;
    }

    printf("恭喜！数据库连接成功！Server代码可能写坏了。\n");
    mysql_close(con);
    return 0;
}