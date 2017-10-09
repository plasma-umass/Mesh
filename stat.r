library(ggplot2)
library(grid)
library(scales)

dat_mesh <- read.delim('./data/mem-mesh.tsv', header = TRUE, sep = '\t')
dat_mesh$alloc <- 'mesh'

dat_glibc <- read.delim('./data/mem-glibc.tsv', header = TRUE, sep = '\t')
dat_glibc$alloc <- 'glibc'

dat_tcmalloc <- read.delim('./data/mem-tcmalloc.tsv', header = TRUE, sep = '\t')
dat_tcmalloc$alloc <- 'tcmalloc'

## dat <- merge(dat_mesh, dat_glibc, dat_tcmalloc)


p <- ggplot() +
    geom_point(data=dat_mesh, size=.5, aes(x=time, y=rss, color=alloc, shape=alloc)) +
    geom_point(data=dat_glibc, size=.5, aes(x=time, y=rss, color=alloc, shape=alloc)) +
    geom_point(data=dat_tcmalloc, size=.5, aes(x=time, y=rss, color=alloc, shape=alloc)) +
    scale_y_continuous('RSS (bytes)') +
    scale_x_discrete('Time Since Program Start') +
    theme_bw(10, 'Times') +
#    labs(title = '%STAT% Samples relative to native') +
    theme(
        plot.title = element_text(size=10, face='bold'),
        strip.background = element_rect(color='dark gray', linetype=0.5),
        plot.margin = unit(c(0, 0, 0, 0), 'in'),
        panel.border = element_rect(colour='gray'),
        panel.margin = unit(c(0, 0, -0.5, 0), 'in'),
        legend.position = 'bottom',
        legend.key = element_rect(color=NA),
        legend.key.size = unit(0.15, 'in'),
        legend.margin = unit(0, 'in'),
        axis.title.y = element_text(size=9),
        axis.text.x = element_text(angle = 45, hjust = 1)
    )

ggsave(p, filename='frag.pdf', width=6, height=4)
