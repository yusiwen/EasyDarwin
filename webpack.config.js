const HtmlWebpackPlugin = require('html-webpack-plugin');
const CleanWebpackPlugin = require('clean-webpack-plugin');
const CopyWebpackPlugin = require('copy-webpack-plugin');
const ExtractTextPlugin = require("extract-text-webpack-plugin");
const OptimizeCssAssetsPlugin = require('optimize-css-assets-webpack-plugin');
const UglifyJsPlugin = require('uglifyjs-webpack-plugin');
const webpack = require('webpack');
const path = require('path');

function resolve(dir) {
    return path.resolve(__dirname, dir)
}

module.exports = {
    entry: {
        "index": ['babel-polyfill', resolve('web_src/index.js')],
        "login": ['babel-polyfill', resolve('web_src/login.js')]
    },
    output: {
        path: resolve('dist'),
        chunkFilename: 'js/[name].[chunkhash:8].js',
        filename: 'js/[name].[chunkhash:8].js'
    },
    externals: {
        'jquery': '$'
    },
    resolve: {
        extensions: ['.js', '.vue', '.json'],
        alias: {
            'vue$': 'vue/dist/vue.common.js'
        }
    },
    devServer: {
        host: '0.0.0.0',
        useLocalIp: true,       
        proxy: {
            "*": {
                target: `http://localhost:10008`,
                secure: false
            }
        }
    },
    module: {
        rules: [{
            test: /\.js$/,
            loader: 'babel-loader',
            exclude: /node_modules/,
        }, {
            test: /pretty-bytes/,
            loader: 'babel-loader'
        }, {
            test: /\.vue$/,
            loader: 'vue-loader',
            options: {
                loaders: {
                    css: ExtractTextPlugin.extract({
                        publicPath: '../',
                        fallback: 'vue-style-loader', //this is a dep of vue-loader, so no need to explicitly install if using npm3
                        use: 'css-loader'
                    })
                },
                postcss: [
                    require('autoprefixer')()
                ]
            }
        }, {
            test: /\.css$/,
            use: ExtractTextPlugin.extract({
                publicPath: '../',
                fallback: "style-loader",
                use: "css-loader"
            })
        }, {
            test: /\.less$/,
            use: ExtractTextPlugin.extract({
                publicPath: '../',
                fallback: "style-loader",
                use: "css-loader!less-loader"
            })
        }, {
            test: /\.scss$/,
            use: ExtractTextPlugin.extract({
                publicPath: '../',
                fallback: "style-loader",
                use: "css-loader!sass-loader"
            })
        }, {
            test: /\.(png|jpe?g|gif|svg)(\?.*)?$/,
            loader: 'url-loader',
            options: {
                outputPath: "images/",
                limit: 10000,
                name: "[name].[hash:8].[ext]"
            }              
        }, {
            test: /\.(woff2?|eot|ttf|otf)(\?.*)?$/,
            loader: 'url-loader',
            options: {
                outputPath: "fonts/",
                limit: 10000,
                name: "[name].[hash:8].[ext]"
            }              
        }, {
            test: /\.(swf|mp4|webm|ogg|mp3|wav|flac|aac)(\?.*)?$/,
            loader: 'url-loader',
            options: {
                outputPath: "media/",
                limit: 10000,
                name: "[name].[hash:8].[ext]"
            }              
        }]
    },
    plugins: [
        new webpack.ProvidePlugin({
            $: 'jquery',
            jQuery: 'jquery',
            "window.jQuery": 'jquery',
            "window.$": 'jquery'
        }),
        new webpack.HashedModuleIdsPlugin(),
        new CopyWebpackPlugin([
            { from: 'web_src/assets/js/jquery-2.2.4.js', to: 'js/' },
            { from: 'web_src/assets/crossdomain.xml' },
            { from: 'web_src/assets/favicon.ico' },
            { from: 'node_modules/easy-player/dist/component/easy-player-lib.min.js', to: 'js/'},
            { from: 'node_modules/easy-player/dist/component/easy-player-fluent.swf'},
            { from: 'node_modules/easy-player/dist/component/easy-player.swf'}
        ]),
        new ExtractTextPlugin("css/[name].[chunkhash:8].css"),
        new HtmlWebpackPlugin({
            filename: 'login.html',
            title: 'EasyDarwin 登录',
            inject: true,
            chunks: ['login'],
            template: 'web_src/template-login.html'
        }),        
        new HtmlWebpackPlugin({
            filename: 'index.html',
            title: 'EasyDarwin',
            inject: true, // head -> Cannot find element: #app
            chunks: ['index'],
            template: 'web_src/template.html'
        })
    ]
};

if (process.env.NODE_ENV === "production") {
    module.exports.plugins = (module.exports.plugins || []).concat([
        new CleanWebpackPlugin(['dist'], {
            root: resolve("./")
        }),
        new webpack.DefinePlugin({
            'process.env': {
                NODE_ENV: '"production"'
            }
        }),
        new OptimizeCssAssetsPlugin({
            assetNameRegExp: /\.css$/g,
            cssProcessor: require('cssnano'),
            cssProcessorOptions: { discardComments: { removeAll: true } },
            canPrint: true
        }),         
        new UglifyJsPlugin({
            uglifyOptions: {
                output: {
                    comments: false,
                },
            },
        })
    ])
}