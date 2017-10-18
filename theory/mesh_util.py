# -*- coding: utf-8 -*-
"""
Created on Mon Jun 19 15:32:02 2017

@author: devd
"""
import math
import operator
from itertools import izip, imap
import random
from scipy.misc import comb as fast_nCr
from scipy.special import gamma


def formatStrings(strings):
    """Adds extra data to a list of strings for ease of meshing.  Replaces each
    string in the list with a tuple (A,B,C,D). A = original string. B = binary 
    representation for fast arithmetic.  C = occupancy.  D = flag that indicates
    whether the string has been meshed(initially set to False)."""
    new_strings = []
    for string in strings:
        #new_strings.append((string, long(string, base=2)))
        new_strings.append(
            (string, long(string, base=2), string.count("1"), False))
    return new_strings


def hamming(str1, str2):
    """Calculates the Hamming distance between two strings of equal length."""
#    if type(str1) == long:
#        str1 = bin(str1)[2:].rjust(len(str2),"0")
    assert len(str1) == len(str2)
    ne = operator.ne
    return sum(imap(ne, str1, str2))


def fast_q(length, occ1, occ2):
    """computes the probability that two strings with given occupancies will
    mesh."""
    result = float((fast_nCr(length - occ2, occ1))) / (fast_nCr(length, occ1))
    return result


def faster_q(length, occ1, occ2):
    numerator = 1
    for i in range(length - occ1, length - occ1 - occ2, -1):
        print(i)
        numerator *= i
    denominator = 1
    for i in range(length, length - occ2, -1):
        denominator *= i
    return float(numerator) / float(denominator)


def generate_cutoffs(bkt1, length, cutoff):
    """returns a dict indexed by string occupancy, value is the cutoff occupancy
    for potential meshes (if you encounter a higher occupancy during a greedy
    search for a mesh, stop)."""
    cutoffs = {}
    for s in bkt1:
        occ1 = s[2]
        if occ1 not in cutoffs.keys():
            cutoffs[occ1] = float('inf')
            # only calculate cutoffs for every 5th occupancy, to save time
            for occ2 in range(0, int(length / 2), 5):
                if faster_q(length, occ1, occ2) < cutoff:
                    cutoffs[occ1] = occ2
                    break
    return cutoffs


class Splitter(object):
    """
    Encapsulates splitting behavior for a trial.

    Keeps track of multiple different splitting strings and can
    automatically cycle through them if required.
    """

    def __init__(self, length):
        self.length = length
        self.splitting_strings = []
        self.num_splitters = int(math.log(length, 2))
        # print self.num_splitters
        for i in range(1, self.num_splitters + 1):
            split_string = ""
            for j in range(2**(i - 1)):
                split_string = split_string + \
                    (("1" * int((length / (2**i)))) +
                     ("0" * (int(length / (2**i)))))
            self.splitting_strings.append(split_string)
        # print self.splitting_strings
        print 'Splitter(%d): %d splitters with strings: %s' % \
            (length, self.num_splitters, self.splitting_strings)

        self.current_method = 0

    def _splitter(self, strings, advance):
        """splits the given string set based on the current splitting string.  
        optionally advances to the next splitting string for future splittings."""
        split = self.splitting_strings[self.current_method]
        if advance:
            self.current_method = self.current_method + 1
        bucket1 = []
        bucket2 = []
        for s in strings:
            diff = hamming(s[0], split)
            if diff < int(self.length * 0.5):
                bucket1.append(s)
            elif diff == int(self.length * 0.5):
                if random.randint(0, 1):
                    bucket1.append(s)
                else:
                    bucket2.append(s)
            else:
                bucket2.append(s)
        return bucket1, bucket2

    def split(self, strings=[], bucket1=[], bucket2=[], advance=True):
        """the outward-facing method for splitting.  gracefully handles both
        a single string set and a """
#        print 'trying to split. current method is {}'.format(self.current_method)
        if strings == [] and bucket1 == [] and bucket2 == []:
            raise Exception('must provide split method with nonempty input')
        if strings != []:
            return self._splitter(strings, advance)
        else:
            if self.current_method >= self.num_splitters:
                return bucket1, bucket2
            else:
                return self._splitter(bucket1 + bucket2, advance)

    def advance(self):
        self.current_method = self.current_method + 1


def occupancySort(strings):
    """Modifies given list of strings in place, sorting them in order of 
    increasing occupancy."""
#    strings.sort(key = lambda x: x[0].count("1"))
    strings.sort(key=lambda x: x[2])


def simple_traverse(meshes, strings, dim=0):
    """probes a list of strings for meshable pairs. the first string is checked
    against the second, third/fourth, etc. mesh and unmeshed string lists are 
    modified in place. returns True if all strings have been meshed; else returns
    False."""
#    print 'here are the strings passed to simple_traverse', strings
#    print 'and dim is', dim
    matched = []
    for i in range(len(strings) - 2, -1 + dim, -2):
        num1 = strings[i][1]
        num2 = strings[i + 1][1]
#        print num1, num2
        if num1 & num2 == 0:
            matched.append(i)
            meshes.append((strings[i], strings[i + 1]))
#            meshes.append(strings[i+1])
#            print "adding mesh {}, {}".format(strings[i], strings[i+1])
    for x in matched:
        del strings[x + 1]
        del strings[x]
    if len(strings) == 0:
        return True
    return False


def traverse(meshes, bucket1=None, bucket2=None, strings=None, extra=False):
    """looks for meshable pairs between the buckets. modifies the buckets and
    the list of found meshes in place.  returns whether or not meshing is done.
    throws an assertion error if only one bucket has anything in it, so the
    caller can resplit the buckets or whatever."""
    if strings != None:
        #        print 'found strings'
        return simple_traverse(strings, meshes)
    if bucket1 == None or bucket2 == None:
        raise Exception(
            'must pass either buckets or string set to traverse function')

    dim = min(len(bucket1), len(bucket2))
    if len(bucket1) == len(bucket2) == 0:
        return True
    assert dim != 0
    matched = []
    if dim == 1:
        num1 = bucket1[0][1]
        num2 = bucket2[0][1]
        if num1 & num2 == 0:
            matched.append(0)
    for i in range(dim - 1, 0, -1):
        num1 = bucket1[i][1]
        num2 = bucket2[i][1]
        if num1 & num2 == 0:
            matched.append(i)
    for x in matched:
        meshes.append((bucket1[x], bucket2[x]))
    # if one bucket is larger than the other, mesh remaining strings among themselves
    if extra:
        #        print 'extra'
        if len(bucket1) != len(bucket2):
            #            print bucket1, bucket2
            #            print 'chosing one'
            bucket = max([bucket1, bucket2], key=lambda x: len(x))
#            print '{} chosen'.format(bucket)
            simple_traverse(meshes, bucket, dim)
#            print bucket

    for x in matched:
        del bucket1[x]
        del bucket2[x]
    return False


def simpleGreedyTraverse(meshes, strings, cutoff=None):
    """given a list of strings, exhaustively checks the first string for meshes,
    then the second, etc.  found meshes are removed from the list.  ends when all
    pairs of remaining strings have been checked. returns whether or not all
    strings have been meshed."""

    length = len(strings)
    strlength = len(strings[0][0])
#    matched = []
    if cutoff:
        cutoffs = generate_cutoffs(strings, strlength, cutoff)
    for i in range(length):
        # if the current string has already been meshed, skip it
        if strings[i][3]:
            continue

        if cutoff:
            current_cutoff = cutoffs[strings[i][2]]
        for j in range(i + 1, length):
            # if current string has already been meshed, skip it
            if strings[j][3]:
                continue

            if cutoff and strings[j][2] >= current_cutoff:
                break

#            if i not in matched and j not in matched: (should be unnecessary now, test soon)
            if not strings[i][3] and not strings[j][3]:
                num1 = strings[i][1]
                num2 = strings[j][1]
                if num1 & num2 == 0:
                    #                    matched.append(i)
                    #                    matched.append(j)
                    strings[i] = (strings[i][0], strings[i]
                                  [1], strings[i][2], True)
                    strings[j] = (strings[j][0], strings[j]
                                  [1], strings[j][2], True)
                    meshes.append((strings[i], strings[j]))
                    break
    for string1, string2 in meshes:
        strings.remove(string1)
        strings.remove(string2)
    if len(strings) == 0:
        return True
    return False


def greedyTraverse(meshes, bucket1=None, bucket2=None, strings=None, cutoff=None):
    """
    Looks for meshable pairs between the buckets greedily (looks
    first at all potential meshes with the first string in bucket1 and
    anything in bucket 2, then the second string in bucket 2 with
    everything in bucket 2, etc.  adds found pairs to meshes in
    place. returns whether or not all strings have been meshed.
    """

    # if only one string list is supplied, search it exhaustively for
    # pairs using a simpler function
    if strings != None:
        return simpleGreedyTraverse(meshes, strings, cutoff)

    if bucket1 == None or bucket2 == None:
        raise Exception(
            'must pass either buckets or string set to traverse function')

    strlength = len(bucket1[0][0])
    len1, len2 = len(bucket1), len(bucket2)
    assert len1 != 0 and len2 != 0
    if cutoff:
        cutoffs = generate_cutoffs(bucket1, strlength, cutoff)
    for i in range(len1):
        if cutoff:
            bkt1cutoff = cutoffs[bucket1[i][2]]

        for j in range(len2):
            # notice when (due to occupancy ordering) there is little hope of finding more meshes
            # for the ith string in bucket 1
            if cutoff and bucket2[j][2] >= bkt1cutoff:
                #                print "doing a break!"
                break
            if not bucket1[i][3] and not bucket2[j][3]:
                num1 = bucket1[i][1]
                num2 = bucket2[j][1]
                if num1 & num2 == 0:
                    bucket1[i] = (bucket1[i][0], bucket1[i]
                                  [1], bucket1[i][2], True)
                    bucket2[j] = (bucket2[j][0], bucket2[j]
                                  [1], bucket2[j][2], True)
                    meshes.append((bucket1[i], bucket2[j]))
    for string1, string2 in meshes:
        #        print "removing {} from bucket1 and {} from bucket2".format(string1, string2)
        bucket1.remove(string1)
        bucket2.remove(string2)
    if len(bucket1) == len(bucket2) == 0:
        return True
    return False


if __name__ == '__main__':
    bkt1 = formatStrings([("11100000"), ("11111000")])
    bkt2 = formatStrings([("00011111"), ("00000111")])
    meshes = []
    greedyTraverse(meshes, bucket1=bkt1, bucket2=bkt2, cutoff=None)
#    occupancySort(bkt1)
    print bkt1, bkt2, meshes
#    print fast_q(64, 25,13)
#    print generate_cutoffs(bkt1, 8)
#    print generate_cutoffs(bkt2, 8)
