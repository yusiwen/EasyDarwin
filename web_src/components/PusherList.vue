<template>
    <div class="container-fluid no-padding">
        <div class="alert alert-success alert-dismissible">
            <small>
                <strong><i class="fa fa-info-circle"></i> 提示 : </strong> 
                屏幕直播工具可以采用<a href="https://github.com/EasyDSS/EasyScreenLive" target="_blank"> EasyScreenLive <i class="fa fa-external-link"></i></a>，
                <span class="push-url-format">推流URL规则: rtsp://{ip}:{port}/{id}</span> ，
                例如 : rtsp://www.easydarwin.org:554/your_stream_id
            </small>
            <button type="button" class="close" data-dismiss="alert" aria-label="Close"><span aria-hidden="true">&times;</span></button>
        </div> 

        <div class="box box-success">
            <div class="box-header">
                <h4 class="text-success text-center">推流列表</h4>           
                <form class="form-inline">
                  <div class="form-group">
                      <button type="button" class="btn btn-sm btn-success" @click.prevent="$refs['pullRTSPDlg'].show()"><i class="fa fa-plus"></i> 拉流分发</button>
                  </div>                  
                  <div class="form-group pull-right">
                      <div class="input-group">
                          <input type="text" class="form-control" placeholder="搜索" v-model.trim="q" @keydown.enter.prevent ref="q">
                          <div class="input-group-btn">
                              <button type="button" class="btn btn-default" @click.prevent="doSearch" >
                                  <i class="fa fa-search"></i>
                              </button>  
                          </div>                            
                      </div>
                  </div>                              
                </form>            
            </div>
            <div class="box-body">
                <el-table :data="pushers" stripe class="view-list" :default-sort="{prop: 'startAt', order: 'descending'}" @sort-change="sortChange">
                    <el-table-column prop="id" label="ID" min-width="120"></el-table-column>
                    <el-table-column label="播放地址" min-width="240" show-overflow-tooltip>
                      <template slot-scope="scope">
                        <span v-for="url in scope.row.url" :key="url" style="display: block;">
                          <i class="fa fa-copy" role="button" v-clipboard="url" title="点击拷贝" @success="$message({type:'success', message:'成功拷贝到粘贴板'})"></i>
                            {{url}}
                          </span>
                      </template>
                    </el-table-column>                    
                    <el-table-column label="源地址" min-width="240" show-overflow-tooltip>
                      <template slot-scope="scope">
                        <span v-if="scope.row.source">
                          <i class="fa fa-copy" role="button" v-clipboard="scope.row.source" title="点击拷贝" @success="$message({type:'success', message:'成功拷贝到粘贴板'})"></i>
                          {{scope.row.source}}
                          </span>
                        <span v-else>-</span>
                      </template>
                    </el-table-column>
                    <el-table-column prop="transType" label="传输方式" min-width="100"></el-table-column>                            
                    <el-table-column prop="inBytes" label="上行流量" min-width="120" :formatter="formatBytes" sortable="custom"></el-table-column>
                    <el-table-column prop="outBytes" label="下行流量" min-width="120" :formatter="formatBytes" sortable="custom"></el-table-column>
                    <el-table-column prop="onlines" label="在线人数" min-width="100" sortable="custom"></el-table-column>
                    <el-table-column prop="startAt" label="开始时间" min-width="200" sortable="custom"></el-table-column>
                    <el-table-column label="操作" min-width="120" fixed="right">
                        <template slot-scope="scope">
                            <div class="btn-group">
                                <a role="button" class="btn btn-xs btn-danger" @click.prevent="del(scope.row)">
                                  <i class="fa fa-trash-o"></i> 删除
                                </a>
                            </div>
                            <div class="btn-group">
                                <a v-if="!scope.row.stopped" role="button" class="btn btn-xs btn-danger" @click.prevent="toggle(scope.row)">
                                  <i class="fa fa-stop"></i> 停止
                                </a>
                                <a v-else role="button" class="btn btn-xs" style="background-color: green;color: white;" @click.prevent="toggle(scope.row)">
                                  <i class="fa fa-hourglass-start"></i> 启动
                                </a>
                            </div>
                            <div class="btn-group">
                                <a v-if="vstatus.get(scope.row.id) == null" role="button" style="background-color: green;color: white;" class="btn btn-xs" @click.prevent="video(scope.row,0)">
                                  <i class="fa fa-toggle-right"></i> 录像
                                </a>
                                <a v-else role="button" class="btn btn-xs btn-danger" @click.prevent="video(scope.row,1)">
                                  <i class="fa fa-toggle-right"></i> 停止
                                </a>
                            </div>
                            <div class="btn-group">
                                <a role="button" class="btn btn-xs btn-danger" @click.prevent="screen(scope.row)">
                                  <i class="fa fa-circle-o"></i> 截图
                                </a>
                            </div>
                        </template>
                    </el-table-column>
                </el-table>
            </div>
            <div class="box-footer clearfix" v-if="total > 0">
                <el-pagination layout="prev,pager,next" class="pull-right" :total="total" :page-size.sync="pageSize" :current-page.sync="currentPage"></el-pagination>
            </div>
        </div>
        <PullRTSPDlg ref="pullRTSPDlg" @submit="getPushers"></PullRTSPDlg>
    </div>
</template>

<script>
import PullRTSPDlg from "./PullRTSPDlg.vue"
import prettyBytes from "pretty-bytes";

import _ from "lodash";
export default {
  components: {
    PullRTSPDlg
  },
  props: {},
  data() {
    return {
      q: "",
      sort: "startAt",
      order: "descending",
      pushers: [],
      total: 0,
      timer: 0,
      pageSize: 10,
      currentPage: 1,
      vstatus: new Map()
    };
  },
  beforeDestroy() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = 0;
    }
  },
  mounted() {
    // this.$refs["q"].focus();
    this.timer = setInterval(() => {
      this.getPushers();
    }, 3000);
  },
  watch: {
    q: function(newVal, oldVal) {
      this.doDelaySearch();
    },
    currentPage: function(newVal, oldVal) {
      this.doSearch(newVal);
    }
  },  
  methods: {
    getPushers() {
      $.get("/api/v1/pushers", {
        q: this.q,
        start: (this.currentPage -1) * this.pageSize,
        limit: this.pageSize,
        sort: this.sort,
        order: this.order
      }).then(data => {
        this.total = data.total;
        this.pushers = data.rows;
        this.pushers.forEach(e => {
          this.vstatus.set(e.id,sessionStorage.getItem(e.id))
        });
      });
    },
    doSearch(page = 1) {
      var query = {};
      if (this.q) query["q"] = this.q;
      this.$router.replace({
        path: `/pushers/${page}`,
        query: query
      });
    },
    doDelaySearch: _.debounce(function() {
      this.doSearch();
    }, 500),
    sortChange(data) {
      this.sort = data.prop;
      this.order = data.order;
      this.getPushers();
    },
    formatBytes(row, col, val) {
      if (val == undefined) return "-";
      return prettyBytes(val);
    },
    toggle(row) {
      this.$confirm(`确认${!row.stopped?"停止":"启动"} ${row.path} ?`, "提示").then(() => {
        $.get("/api/v1/stream/toggle", {
          id: row.id
        }).then(data => {
          this.getPushers();
        })
      }).catch(() => {});
    },
    del(row) {
      console.log(row.id)
      this.$confirm(`确认删除 ${row.path} ?`, "提示").then(() => {
        $.get("/api/v1/stream/delete", {
          id: row.id
        }).then(data => {
          this.getPushers();
        })
      }).catch(() => {});
    },
    video(row,flag) {
      console.log(row.id);
      var date = Date.now();
      this.$confirm(`确认 ${flag==0?"录屏":"停止"} ?`, "提示").then(() => {
        if(flag==0){
          $.get("/api/v1/record/start", {
            streamId: row.id,
            tag: date
          }).then(data => {
            this.getPushers();
            this.vstatus.set(row.id,date)
            sessionStorage.setItem(row.id,date)
            console.info(this.vstatus)
          })
        }else{
          $.get("/api/v1/record/stop", {
            streamId: row.id,
            tag: this.vstatus.get(row.id)
          }).then(data => {
            this.getPushers();
            this.vstatus.delete(row.id)
            sessionStorage.removeItem(row.id)
          })
        }
      }).catch(() => {});
    },
    screen(row) {
      console.log(row.id)
      this.$confirm(`确认截屏 ${row.path} ?`, "提示").then(() => {
        var date = Date.now();
        console.log(row.id,date)
        $.get("/api/v1/record/screenshot", {
          streamId: row.id,
          tag: date
        }).then(data => {
          this.getPushers();
        })
      }).catch(() => {});
    }
  },
  beforeRouteEnter(to, from, next) {
    next(vm => {
      vm.q = to.query.q || "";
      vm.currentPage = parseInt(to.params.page) || 1;
    });
  },
  beforeRouteUpdate(to, from, next) {
    next();
    this.$nextTick(() => {
      this.q = to.query.q || "";
      this.currentPage = parseInt(to.params.page) || 1;
      this.pushers = [];
      this.getPushers();
    });
  }
};
</script>

